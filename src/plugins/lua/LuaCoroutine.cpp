/*
 * MacroQuest: The extension platform for EverQuest
 * Copyright (C) 2002-2021 MacroQuest Authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "pch.h"
#include "LuaCoroutine.h"
#include "LuaThread.h"

#include <mq/Plugin.h>

namespace mq::lua {

CoroutineResult LuaCoroutine::RunCoroutine(const std::vector<std::string>& args)
{
	try
	{
		luaThread->SetCurrentCoroutine(this);
		auto result = coroutine(sol::as_args(args));
		if (result.valid())
			return result;

		LuaError("%s", sol::stack::get<std::string>(result.lua_state(), result.stack_index()).c_str());
		DebugStackTrace(result.lua_state());
		result.abandon();
	}
	catch (const sol::error& ex)
	{
		LuaError("%s", ex.what());
		DebugStackTrace(coroutine.lua_state());
	}

	return std::nullopt;
}

LuaCoroutine::LuaCoroutine(sol::thread& thread, LuaThread* luaThread)
	: thread(thread)
	, luaThread(luaThread)
{
}

std::shared_ptr<LuaCoroutine> LuaCoroutine::Create(sol::thread& thread, LuaThread* luaThread)
{
	auto co_ptr = std::make_shared<LuaCoroutine>(thread, luaThread);
	return co_ptr;
}

bool LuaCoroutine::CheckCondition(std::optional<sol::function>& func)
{
	if (!func)
		return false;

	try
	{
		auto check_thread = sol::thread::create(thread.state());
		sol::function check(check_thread.state(), *func);
		return check();
	}
	catch (sol::error& ex)
	{
		LuaError("Failed to check delay condition check with error '%s'", ex.what());
		func = std::nullopt;
	}

	return false;
}

void LuaCoroutine::Delay(sol::object delayObj, sol::object conditionObj)
{
	using namespace std::chrono_literals;

	auto delay_int = delayObj.as<std::optional<const int64_t>>();
	if (!delay_int)
	{
		auto delay_str = delayObj.as<std::optional<std::string_view>>();
		if (delay_str)
		{
			if (delay_str->length() > 1 && delay_str->compare(delay_str->length() - 1, 1, "m") == 0)
			{
				delay_int.emplace(std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::minutes(GetIntFromString(*delay_str, 0))).count());
			}
			else if (delay_str->length() > 2 && delay_str->compare(delay_str->length() - 2, 2, "ms") == 0)
			{
				delay_int.emplace(std::chrono::milliseconds(GetIntFromString(*delay_str, 0)).count());
			}
			else if (delay_str->length() > 1 && delay_str->compare(delay_str->length() - 1, 1, "s") == 0)
			{
				delay_int.emplace(std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::seconds(GetIntFromString(*delay_str, 0))).count());
			}
		}
	}

	if (delay_int.has_value())
	{
		uint64_t delay_ms = std::max(0ms, std::chrono::milliseconds(*delay_int)).count();
		std::optional<sol::function> condition = conditionObj.as<std::optional<sol::function>>();

		SetDelay(delay_ms + MQGetTickCount64(), condition);
	}
}

void LuaCoroutine::SetDelay(uint64_t time, std::optional<sol::function> condition /* = std::nullopt */)
{
	if (luaThread == nullptr || luaThread->IsPaused())
		return;

	if (time > MQGetTickCount64() && !CheckCondition(condition))
	{
		luaThread->DoYield();
		//lua_yield(coroutine.lua_state(), 0); // only yield from the current coroutine
		m_delayTime = time;
		m_delayCondition = condition;
	}
}

void LuaCoroutine::ClearDelay()
{
	m_delayTime = 0L;
	m_delayCondition = std::nullopt;
}

bool LuaCoroutine::ShouldRun()
{
	if (luaThread == nullptr || luaThread->IsPaused())
	{
		return false;
	}

	// check delayed status
	if (m_delayTime <= MQGetTickCount64() || CheckCondition(m_delayCondition))
	{
		ClearDelay();
		return true;
	}

	return false;
}

} // namespace mq::lua