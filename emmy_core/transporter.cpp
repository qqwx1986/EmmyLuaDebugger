/*
* Copyright (c) 2019. tangzx(love.tangzx@qq.com)
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#include "transporter.h"
#include "emmy_facade.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "functional"
#include <type_traits>
static void log(const std::string& info)
{
	// printf("EmmyLuaDebugLog : %s", info.c_str());
	// static std::string FinalFile;
	// static struct Init {
	// 	Init() {
	// 		std::string File = "d:/emmy1.txt";
	// 		auto f = fopen(File.c_str(), "r");
	// 		if (f) {
	// 			FinalFile = "d:/emmy2.txt";
	// 			fclose(f);
	// 		}
	// 		else {
	// 			FinalFile = File;
	// 		}
	// 	}
	// } _;
	//
	// auto f = ::fopen(FinalFile.c_str(), "a+");
	// fwrite(info.c_str(), 1, info.size(), f);
	// fclose(f);
}
Transporter::Transporter(bool server):
	facade(nullptr),
	receiveSize(0),
	readHead(true),
	running(false),
	connected(false),
	serverMode(server),
	threadSafe(false){
	loop = uv_loop_new();
	bufSize = 10 * 1024;
	buf = static_cast<char*>(malloc(bufSize));
}

Transporter::~Transporter() {
	if (buf) {
		free(buf);
	}
	Stop();
	if (thread.joinable())
		thread.join();
}

void Transporter::Send(int cmd, const rapidjson::Document& document) {
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	document.Accept(writer);
	if (cmd != int(MessageCMD::LogNotify))
	{
		EmmyFacade::Get()->SendLog(LogType::Debug, "Transporter::Send === %s \n", buffer.GetString());
	}
	Send(cmd, buffer.GetString(), buffer.GetSize());
}

void Transporter::SetHandler(EmmyFacade* facade) {
	this->facade = facade;
}

void Transporter::OnAfterRead(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf) {
	if (nread < 0) {
		printf("[%d]Transporter::OnAfterRead %d\n",port,nread);
		/* Error or EOF */
		free(buf->base);
		uv_close(reinterpret_cast<uv_handle_t*>(handle), nullptr);

		// on disconnect
		OnDisconnect();
		return;
	}

	if (nread == 0) {
		/* Everything OK, but nothing read. */
		free(buf->base);
		return;
	}

	Receive(buf->base, nread);
}

void Transporter::Receive(const char* data, size_t len) {
	const auto remain = bufSize - receiveSize;
	if (remain < len) {
		buf = static_cast<char*>(realloc(buf, bufSize + len));
	}
	memcpy(buf + receiveSize, data, len);
	receiveSize += len;

	size_t pos = 0;
	while (true) {
		size_t start = pos;
		for (size_t i = pos; i < receiveSize; i++) {
			if (data[i] == '\n') {
				pos = i + 1;
				break;
			}
		}
		if (start != pos) {
			if (readHead) {
				// skip
			}
			else {
				rapidjson::Document document;
				document.Parse(buf + start, pos - start);
				std::string str(buf + start, pos - start);
				str += '\0';
				log("recv "+ str + "\n");
				EmmyFacade::Get()->SendLog(LogType::Debug,"recv %s\n" ,str.c_str());
				OnReceiveMessage(document);
			}
			readHead = !readHead;
		}
		else break;
	}

	if (pos > 0) {
		memcpy(buf, buf + pos, receiveSize - pos);
		receiveSize -= pos;
	}
}

void Transporter::OnReceiveMessage(const rapidjson::Document& document) {
	if (threadSafe)
	{
		const auto cmd = static_cast<MessageCMD>(document["cmd"].GetInt());
		switch (cmd) {
		case MessageCMD::ActionReq:
		case MessageCMD::EvalReq:
			if (facade) {
				facade->OnReceiveMessage(document);
			}
			break;
			default:
				serverRecvMsg.push(new rapidjson::Document(std::forward<rapidjson::Document>(const_cast<rapidjson::Document&>(document))));
			break;
		}
	}
	else
	{
		if (facade) {
			facade->OnReceiveMessage(document);
		}
	}
}

void Transporter::OnDisconnect() {
	connected = false;
	readHead = true;
	receiveSize = 0;
	if (facade) {
		facade->OnDisconnect();
	}
}

void Transporter::OnConnect(bool suc) {
	connected = suc;
	readHead = true;
	receiveSize = 0;
	if (facade) {
		facade->OnConnect(suc);
	}
}

bool Transporter::IsConnected() const {
	return connected;
}

bool Transporter::IsServerMode() const {
	return serverMode;
}

////////////////////////////////////////////////////////////////////////////////
// send data

typedef struct {
	uv_write_t req;
	uv_buf_t buf;
	uv_stream_t* handler;
} write_req_t;

static void after_write(uv_write_t* req, int status) {
	const auto* writeReq = reinterpret_cast<write_req_t*>(req);
	free(writeReq->buf.base);
	delete writeReq;
}

static void after_async(uv_handle_t* h) {
	delete h;
}

static void async_write(uv_async_t* h) {
	auto* writeReq = (write_req_t*)h->data;
	uv_write(&writeReq->req, writeReq->handler, &writeReq->buf, 1, after_write);
	uv_close((uv_handle_t*)h, after_async);
}

void Transporter::Send(uv_stream_t* handler, int cmd, const char* data, size_t len) {
	if (!IsConnected()) {
		return;
	}
	auto* writeReq = new write_req_t();
	char cmdValue[100];
	const int l1 = sprintf(cmdValue, "%d\n", cmd);
	const size_t newLen = len + l1 + 1;
	char* newData = static_cast<char*>(malloc(newLen));
	// line1
	memcpy(newData, cmdValue, l1);
	// line2
	memcpy(newData + l1, data, len);
	newData[newLen - 1] = '\n';
	writeReq->buf = uv_buf_init(newData, newLen);
	writeReq->handler = handler;

	// thread safe:
	auto* async = new uv_async_t;
	async->data = writeReq;
	uv_async_init(loop, async, async_write);
	uv_async_send(async);
}

void Transporter::StartEventLoop() {
	thread = std::thread(std::bind(&Transporter::Run, this));
}
void Transporter::EventLoop()
{
	rapidjson::Document *document;
	while (serverRecvMsg.try_pop(document))
	{
		if (facade) {
			facade->OnReceiveMessage(*document);
		}
		delete document;
	}
}

void Transporter::Run() {
	running = true;
	while (running) {
		uv_run(loop, UV_RUN_NOWAIT);
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}

int Transporter::Stop() {
	printf("[%d]Transporter::Stop\n",port);
	running = false;
	return 0;
}
