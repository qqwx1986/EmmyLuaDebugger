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
#pragma once
#include "emmy_core.h"
#include <rapidjson/document.h>
#include <mutex>
#include <condition_variable>
#include <set>

class Transporter;
class EvalContext;

enum class LogType {
	Info, Warning, Error
};

class EmmyFacade {
	Transporter* transporter;
	lua_State* L;
	std::mutex waitIDEMutex;
	std::condition_variable waitIDECV;
	bool isIDEReady;
	bool isWaitingForIDE;
public:
	static EmmyFacade* Get();
	EmmyFacade();
	~EmmyFacade();
	bool TcpListen(lua_State* L, const std::string& host, int port, std::string& err);
	bool TcpConnect(lua_State* L, const std::string& host, int port, std::string& err);
	bool PipeListen(lua_State* L, const std::string& name, std::string& err);
	bool PipeConnect(lua_State* L, const std::string& name, std::string& err);
	int BreakHere(lua_State* L);
	int OnConnect(bool suc);
	int OnDisconnect();
	void WaitIDE(bool force = false);
	void OnReceiveMessage(const rapidjson::Document& document);
	void OnBreak();
	void Destroy();
	void OnEvalResult(EvalContext* context);
	void SendLog(LogType type, const char *fmt, ...);
	void OnLuaStateGC(lua_State* L);
private:
	void OnInitReq(const rapidjson::Document& document);
	void OnReadyReq(const rapidjson::Document& document);
	void OnAddBreakPointReq(const rapidjson::Document& document);
	void OnRemoveBreakPointReq(const rapidjson::Document& document);
	void OnActionReq(const rapidjson::Document& document);
	void OnEvalReq(const rapidjson::Document& document);
#ifdef EMMY_BUILD_AS_HOOK
private:
	std::set<lua_State*> attachedStates;
public:
	void StartupHookMode(int port);
	void Attach(lua_State* L);
	void StartHook();
#endif
};
