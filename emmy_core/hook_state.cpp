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
#include "hook_state.h"
#include "emmy_debugger.h"

HookState::HookState():
	currentStateL(nullptr) {
}

void HookState::Start(Debugger* debugger, lua_State* L, lua_State* current) {
	currentStateL = current;
}

void HookState::ProcessHook(Debugger* debugger, lua_State* L, lua_Debug* ar) {
}

void HookStateContinue::Start(Debugger* debugger, lua_State* L, lua_State* current) {
	debugger->ExitDebugMode();
}

void StackLevelBasedState::Start(Debugger* debugger, lua_State* L, lua_State* current) {
	HookState::Start(debugger, L, current);
	oriStackLevel = newStackLevel = debugger->GetStackLevel(current, false);
}

void StackLevelBasedState::UpdateStackLevel(Debugger* debugger, lua_State* L, lua_Debug* ar) {
	if (L != currentStateL) {
		return;
	}
	// this is needed to check if the stack got shorter or longer.
	// unfortunately counting call/return calls is not reliable.
	// the discrepancy may happen when "pcall(load, '')" call is made
	// or when "error()" is called in a function.
	// in either case there are more "call" than "return" events reported.
	// this validation is done for every "line" event, but should be "cheap"
	// as it checks for the stack to get shorter (or longer by one call).
	// start from one level higher just in case we need to grow the stack.
	// this may happen after coroutine.resume call to a function that doesn't
	// have any other instructions to execute. it triggers three returns:
	// "return, tail return, return", which needs to be accounted for.
	
	//newStackLevel = debugger->GetStackLevel(L, true);

	// "cheap" version
	lua_Debug ar2{};
	for (int i = newStackLevel + 1; i >= 0; --i) {
		if (lua_getstack(L, i, &ar2)) {
			newStackLevel = i + 1;
			break;
		}
	}
}

void HookStateStepIn::Start(Debugger* debugger, lua_State* L, lua_State* current) {
	StackLevelBasedState::Start(debugger, L, current);
	lua_Debug ar{};
	lua_getstack(current, 0, &ar);
	lua_getinfo(current, "nSl", &ar);
	file = ar.source;
	line = ar.currentline;
	debugger->ExitDebugMode();
}

void HookStateStepIn::ProcessHook(Debugger* debugger, lua_State* L, lua_Debug* ar) {
	UpdateStackLevel(debugger, L, ar);
	if (ar->event == LUA_HOOKLINE && ar->currentline != line) {
		// todo : && file != ar->source
		debugger->HandleBreak(L);
	}
	else StackLevelBasedState::ProcessHook(debugger, L, ar);
}

void HookStateStepOut::Start(Debugger* debugger, lua_State* L, lua_State* current) {
	StackLevelBasedState::Start(debugger, L, current);
	debugger->ExitDebugMode();
}

void HookStateStepOut::ProcessHook(Debugger* debugger, lua_State* L, lua_Debug* ar) {
	UpdateStackLevel(debugger, L, ar);
	if (newStackLevel < oriStackLevel) {
		debugger->HandleBreak(L);
		return;
	}
	StackLevelBasedState::ProcessHook(debugger, L, ar);
}

void HookStateStepOver::Start(Debugger* debugger, lua_State* L, lua_State* current) {
	StackLevelBasedState::Start(debugger, L, current);
	lua_Debug ar{};
	lua_getstack(current, 0, &ar);
	lua_getinfo(current, "nSl", &ar);
	file = ar.source;
	line = ar.currentline;
	debugger->ExitDebugMode();
}

void HookStateStepOver::ProcessHook(Debugger* debugger, lua_State* L, lua_Debug* ar) {
	UpdateStackLevel(debugger, L, ar);
	// step out
	if (newStackLevel < oriStackLevel) {
		debugger->HandleBreak(L);
		return;
	}
	if (ar->event == LUA_HOOKLINE &&
		ar->currentline != line &&
		newStackLevel == oriStackLevel) {
		lua_getinfo(L, "Sl", ar);
		if (ar->source == file || line == -1) {
			debugger->HandleBreak(L);
			return;
		}
	}
	StackLevelBasedState::ProcessHook(debugger, L, ar);
}

void HookStateBreak::ProcessHook(Debugger* debugger, lua_State* L, lua_Debug* ar) {
	if (ar->event == LUA_HOOKLINE) {
		debugger->HandleBreak(L);
	}
	else {
		HookState::ProcessHook(debugger, L, ar);
	}
}

void HookStateStop::Start(Debugger* debugger, lua_State* L, lua_State* current) {
	debugger->UpdateHook(L, 0);
	debugger->DoAction(DebugAction::Continue);
}
