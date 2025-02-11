#include "Commands_Script.h"

#include <filesystem>

#include "ParamInfos.h"
#include "GameScript.h"
#include "MemoizedMap.h"
#include "ScriptUtils.h"
#include "PluginManager.h"
#include "ScriptAnalyzer.h"

#if RUNTIME
#include "GameAPI.h"
#include "GameForms.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include "GameData.h"

#include "EventManager.h"
#include "FunctionScripts.h"
#include <fstream>
//#include "ModTable.h"

enum EScriptMode
{
	eScript_HasScript,
	eScript_Get,
	eScript_Remove,
};

static bool GetScript_Execute(COMMAND_ARGS, EScriptMode eMode)
{
	*result = 0;
	TESForm *form = NULL;

	ExtractArgsEx(EXTRACT_ARGS_EX, &form);
	bool parmForm = form ? true : false;

	form = form->TryGetREFRParent();
	if (!form)
	{
		if (!thisObj)
			return true;
		form = thisObj->baseForm;
	}

	TESScriptableForm *scriptForm = DYNAMIC_CAST(form, TESForm, TESScriptableForm);
	Script *script = NULL;
	EffectSetting *effect = NULL;
	if (!scriptForm) // Let's try for a MGEF
	{
		effect = DYNAMIC_CAST(form, TESForm, EffectSetting);
		if (effect)
			script = effect->GetScript();
	}
	else
		script = (scriptForm) ? scriptForm->script : NULL;

	switch (eMode)
	{
	case eScript_HasScript:
	{
		*result = (script != NULL) ? 1 : 0;
		break;
	}
	case eScript_Get:
	{
		if (script)
		{
			UInt32 *refResult = (UInt32 *)result;
			*refResult = script->refID;
		}
		break;
	}
	case eScript_Remove:
	{
		// simply forget about the script
		if (script)
		{
			UInt32 *refResult = (UInt32 *)result;
			*refResult = script->refID;
		}
		if (scriptForm)
			scriptForm->script = NULL;
		else if (effect)
			effect->RemoveScript();
		if (!parmForm && thisObj)
		{
			// Remove ExtraScript entry - otherwise the script will keep running until the reference is reloaded.
			thisObj->extraDataList.RemoveByType(kExtraData_Script);
		}
		break;
	}
	}
	return true;
}

bool Cmd_IsScripted_Execute(COMMAND_ARGS)
{
	return GetScript_Execute(PASS_COMMAND_ARGS, eScript_HasScript);
}

bool Cmd_GetScript_Execute(COMMAND_ARGS)
{
	return GetScript_Execute(PASS_COMMAND_ARGS, eScript_Get);
}

bool Cmd_RemoveScript_Execute(COMMAND_ARGS)
{
	return GetScript_Execute(PASS_COMMAND_ARGS, eScript_Remove);
}

bool Cmd_SetScript_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32 *refResult = (UInt32 *)result;

	TESForm *form = NULL;
	TESForm *pForm = NULL;
	TESForm *scriptArg = NULL;

	ExtractArgsEx(EXTRACT_ARGS_EX, &scriptArg, &form);
	bool parmForm = form ? true : false;

	form = form->TryGetREFRParent();
	if (!form)
	{
		if (!thisObj)
			return true;
		form = thisObj->baseForm;
	}

	TESScriptableForm *scriptForm = DYNAMIC_CAST(form, TESForm, TESScriptableForm);
	Script *oldScript = NULL;
	EffectSetting *effect = NULL;
	if (!scriptForm) // Let's try for a MGEF
	{
		effect = DYNAMIC_CAST(form, TESForm, EffectSetting);
		if (effect)
			oldScript = effect->GetScript();
		else
			return true;
	}
	else
		oldScript = scriptForm->script;

	Script *script = DYNAMIC_CAST(scriptArg, TESForm, Script);
	if (!script)
		return true;

	// we can only get a magic script here for an EffectSetting
	if (script->IsMagicScript() && !effect)
		return true;

	// we can't get an unknown script here
	if (script->IsUnkScript())
		return true;

	if (script->IsMagicScript())
	{
		effect->SetScript(script);
		// clean up event list here?
	}
	else if (effect) // we need a magic script and some var won't be initialized
		return true;

	if (oldScript)
	{
		*refResult = oldScript->refID;
	}

	if ((script->IsQuestScript() && form->typeID == kFormType_TESQuest) || script->IsObjectScript())
	{
		scriptForm->script = script;
		// clean up event list here?
		// This is necessary in order to make sure the script uses the correct questDelayTime.
		script->quest = DYNAMIC_CAST(form, TESForm, TESQuest);
	}
	if (script->IsObjectScript() && !parmForm && thisObj)
	{
		// Re-building ExtraScript entry - the new script then starts running immediately (instead of only on reload).
		thisObj->extraDataList.RemoveByType(kExtraData_Script);
		thisObj->extraDataList.Add(ExtraScript::Create(form, true));

		// Commented out until solved

		//if (thisObj->extraDataList.Add(ExtraScript::Create(form, true))) {
		//	ExtraScript* xScript = (ExtraScript*)thisObj->extraDataList.GetByType(kExtraData_Script);
		//	DoCheckScriptRunnerAndRun(thisObj, &(thisObj->extraDataList));
		//	//MarkBaseExtraListScriptEvent(thisObj, &(thisObj->extraDataList), ScriptEventList::kEvent_OnLoad);
		//	if (xScript) {
		//		xScript->EventCreate(ScriptEventList::kEvent_OnLoad, NULL);
		//	}
		//}
	}
	return true;
}

bool Cmd_IsFormValid_Execute(COMMAND_ARGS)
{
	TESForm *pForm = NULL;
	*result = 0;
	if (ExtractArgsEx(EXTRACT_ARGS_EX, &pForm))
	{
		pForm = pForm->TryGetREFRParent();
		if (!pForm)
		{
			if (!thisObj)
				return true;
			pForm = thisObj->baseForm;
		}
		if (pForm)
		{
			*result = 1;
		}
		if (IsConsoleMode())
			Console_Print(*result == 1.0 ? "Valid Form!" : "Invalid Form");
	}
	return true;
}

bool Cmd_IsReference_Execute(COMMAND_ARGS)
{
	TESObjectREFR *refr = NULL;
	*result = 0;
	if (ExtractArgs(EXTRACT_ARGS, &refr))
		*result = 1;
	if (IsConsoleMode())
		Console_Print(*result == 1.0 ? "IsReference" : "Not reference!");

	return true;
}

enum
{
	eScriptVar_Get = 1,
	eScriptVar_GetRef,
	eScriptVar_Has,
};

bool GetVariable_Execute(COMMAND_ARGS, UInt32 whichAction)
{
	char varName[256];
	TESQuest *quest = NULL;
	Script *targetScript = NULL;
	ScriptEventList *targetEventList = NULL;
	*result = 0;

	if (!ExtractArgs(EXTRACT_ARGS, &varName, &quest))
		return true;
	if (quest)
	{
		TESScriptableForm *scriptable = DYNAMIC_CAST(quest, TESQuest, TESScriptableForm);
		targetScript = scriptable->script;
		targetEventList = quest->scriptEventList;
	}
	else if (thisObj)
	{
		TESScriptableForm *scriptable = DYNAMIC_CAST(thisObj->baseForm, TESForm, TESScriptableForm);
		if (scriptable)
		{
			targetScript = scriptable->script;
			targetEventList = thisObj->GetEventList();
		}
	}

	if (targetScript && targetEventList)
	{
		VariableInfo *varInfo = targetScript->GetVariableByName(varName);
		if (varInfo)
		{
			if (whichAction == eScriptVar_Has)
				*result = 1;
			else
			{
				ScriptLocal *var = targetEventList->GetVariable(varInfo->idx);
				if (var)
				{
					if (whichAction == eScriptVar_Get)
						*result = var->data;
					else if (whichAction == eScriptVar_GetRef)
					{
						UInt32 *refResult = (UInt32 *)result;
						*refResult = (*(UInt64 *)&var->data);
					}
				}
			}
		}
	}

	return true;
}

bool Cmd_SetVariable_Execute(COMMAND_ARGS)
{
	char varName[256];
	TESQuest *quest = NULL;
	Script *targetScript = NULL;
	ScriptEventList *targetEventList = NULL;
	float value = 0;
	*result = 0;

	if (!ExtractArgs(EXTRACT_ARGS, &varName, &value, &quest))
		return true;
	if (quest)
	{
		TESScriptableForm *scriptable = DYNAMIC_CAST(quest, TESQuest, TESScriptableForm);
		targetScript = scriptable->script;
		targetEventList = quest->scriptEventList;
	}
	else if (thisObj)
	{
		TESScriptableForm *scriptable = DYNAMIC_CAST(thisObj->baseForm, TESForm, TESScriptableForm);
		if (scriptable)
		{
			targetScript = scriptable->script;
			targetEventList = thisObj->GetEventList();
		}
	}

	if (targetScript && targetEventList)
	{
		VariableInfo *varInfo = targetScript->GetVariableByName(varName);
		if (varInfo)
		{
			ScriptLocal *var = targetEventList->GetVariable(varInfo->idx);
			if (var)
				var->data = value;
		}
	}

	return true;
}

bool Cmd_SetRefVariable_Execute(COMMAND_ARGS)
{
	char varName[256];
	TESQuest *quest = NULL;
	Script *targetScript = NULL;
	ScriptEventList *targetEventList = NULL;
	TESForm *value = NULL;
	*result = 0;

	if (!ExtractArgs(EXTRACT_ARGS, &varName, &value, &quest))
		return true;
	if (quest)
	{
		TESScriptableForm *scriptable = DYNAMIC_CAST(quest, TESQuest, TESScriptableForm);
		targetScript = scriptable->script;
		targetEventList = quest->scriptEventList;
	}
	else if (thisObj)
	{
		TESScriptableForm *scriptable = DYNAMIC_CAST(thisObj->baseForm, TESForm, TESScriptableForm);
		if (scriptable)
		{
			targetScript = scriptable->script;
			targetEventList = thisObj->GetEventList();
		}
	}

	if (targetScript && targetEventList)
	{
		VariableInfo *varInfo = targetScript->GetVariableByName(varName);
		if (varInfo)
		{
			ScriptLocal *var = targetEventList->GetVariable(varInfo->idx);
			if (var)
			{
				UInt32 *refResult = (UInt32 *)result;
				(*(UInt64 *)&var->data) = value ? value->refID : 0;
			}
		}
	}

	return true;
}

bool Cmd_HasVariable_Execute(COMMAND_ARGS)
{
	GetVariable_Execute(PASS_COMMAND_ARGS, eScriptVar_Has);
	return true;
}

bool Cmd_GetVariable_Execute(COMMAND_ARGS)
{
	GetVariable_Execute(PASS_COMMAND_ARGS, eScriptVar_Get);
	return true;
}

bool Cmd_GetRefVariable_Execute(COMMAND_ARGS)
{
	GetVariable_Execute(PASS_COMMAND_ARGS, eScriptVar_GetRef);
	return true;
}

bool Cmd_GetArrayVariable_Execute(COMMAND_ARGS)
{
	if (!ExpressionEvaluator::Active())
	{
		ShowRuntimeError(scriptObj, "GetArrayVariable must be called within the context of an NVSE expression");
		return false;
	}

	GetVariable_Execute(PASS_COMMAND_ARGS, eScriptVar_Get);
	return true;
}

bool Cmd_CompareScripts_Execute(COMMAND_ARGS)
{
	Script *script1 = NULL;
	Script *script2 = NULL;
	*result = 0;

	if (!ExtractArgsEx(EXTRACT_ARGS_EX, &script1, &script2))
		return true;
	script1 = DYNAMIC_CAST(script1, TESForm, Script);
	script2 = DYNAMIC_CAST(script2, TESForm, Script);

	if (script1 && script2 && script1->info.dataLength == script2->info.dataLength)
	{
		if (script1 == script2)
			*result = 1;
		else if (!memcmp(script1->data, script2->data, script1->info.dataLength))
			*result = 1;
	}

	return true;
}

bool Cmd_ResetAllVariables_Execute(COMMAND_ARGS)
{
	//sets all vars to 0
	*result = 0;

	ScriptEventList *list = eventList; //reset calling script by default
	if (thisObj)					   //call on a reference to reset that ref's script vars
		list = thisObj->GetEventList();

	if (list)
		*result = list->ResetAllVariables();

	return true;
}

class ExplicitRefFinder
{
public:
	bool Accept(const Script::RefVariable *var)
	{
		if (var && var->varIdx == 0)
			return true;

		return false;
	}
};

Script *GetScriptArg(TESObjectREFR *thisObj, TESForm *form)
{
	Script *targetScript = NULL;
	if (form)
		targetScript = DYNAMIC_CAST(form, TESForm, Script);
	else if (thisObj)
	{
		TESScriptableForm *scriptable = DYNAMIC_CAST(thisObj->baseForm, TESForm, TESScriptableForm);
		if (scriptable)
			targetScript = scriptable->script;
	}

	return targetScript;
}

bool Cmd_GetNumExplicitRefs_Execute(COMMAND_ARGS)
{
	TESForm *form = NULL;
	Script *targetScript = NULL;
	*result = 0;

	if (ExtractArgsEx(EXTRACT_ARGS_EX, &form))
	{
		targetScript = GetScriptArg(thisObj, form);
		if (targetScript)
			*result = targetScript->refList.CountIf(ExplicitRefFinder());
	}

	if (IsConsoleMode())
		Console_Print("GetNumExplicitRefs >> %.0f", *result);

	return true;
}

bool Cmd_GetNthExplicitRef_Execute(COMMAND_ARGS)
{
	TESForm *form = NULL;
	UInt32 refIdx = 0;
	UInt32 *refResult = (UInt32 *)result;
	*refResult = 0;

	if (ExtractArgsEx(EXTRACT_ARGS_EX, &refIdx, &form))
	{
		Script *targetScript = GetScriptArg(thisObj, form);
		if (targetScript)
		{
			UInt32 count = 0;
			const Script::RefVariable *entry = NULL;
			while (count <= refIdx)
			{
				entry = targetScript->refList.Find(ExplicitRefFinder());
				if (!entry)
					break;

				count++;
			}

			if (count == refIdx + 1 && entry && entry->form)
			{
				*refResult = entry->form->refID;
				if (IsConsoleMode())
					Console_Print("GetNthExplicitRef >> %s (%08x)", GetFullName(entry->form), *refResult);
			}
		}
	}

	return true;
}

bool Cmd_RunScript_Execute(COMMAND_ARGS)
{
	TESForm *form = NULL;

	if (ExtractArgsEx(EXTRACT_ARGS_EX, &form))
	{

		form = form->TryGetREFRParent();
		if (!form)
		{
			if (!thisObj)
				return true;
			form = thisObj->baseForm;
		}

		TESScriptableForm *scriptForm = DYNAMIC_CAST(form, TESForm, TESScriptableForm);
		Script *script = NULL;
		EffectSetting *effect = NULL;
		if (!scriptForm) // Let's try for a MGEF
		{
			effect = DYNAMIC_CAST(form, TESForm, EffectSetting);
			if (effect)
				script = effect->GetScript();
			else
			{
				script = DYNAMIC_CAST(form, TESForm, Script);
			}
		}
		else
			script = scriptForm->script;

		if (script)
		{
			bool runResult = CALL_MEMBER_FN(script, Execute)(thisObj, 0, 0, 0);
			Console_Print("ran script, returned %s", runResult ? "true" : "false");
		}
	}

	return true;
}

bool Cmd_GetCurrentScript_Execute(COMMAND_ARGS)
{
	// apparently this is useful
	UInt32 *refResult = (UInt32 *)result;
	*refResult = scriptObj->refID;
	return true;
}

bool Cmd_GetCallingScript_Execute(COMMAND_ARGS)
{
	UInt32 *refResult = (UInt32 *)result;
	*refResult = 0;
	Script *caller = UserFunctionManager::GetInvokingScript(scriptObj);
	if (caller)
	{
		*refResult = caller->refID;
	}

	return true;
}

bool ExtractEventCallback(ExpressionEvaluator &eval, EventManager::EventCallback *outCallback, char *outName)
{
	if (eval.ExtractArgs() && eval.NumArgs() >= 2)
	{
		const char *eventName = eval.Arg(0)->GetString();
		Script *script = DYNAMIC_CAST(eval.Arg(1)->GetTESForm(), TESForm, Script);
		if (eventName && script)
		{
			outCallback->script = script;
			strcpy_s(outName, 0x20, eventName);

			// any filters?
			for (UInt32 i = 2; i < eval.NumArgs(); i++)
			{
				const TokenPair *pair = eval.Arg(i)->GetPair();
				if (pair && pair->left && pair->right)
				{
					const char *key = pair->left->GetString();
					if (key)
					{
						if (!StrCompare(key, "ref") || !StrCompare(key, "first"))
						{
							outCallback->source = pair->right->GetTESForm();
						}
						else if (!StrCompare(key, "object") || !StrCompare(key, "second"))
						{
							outCallback->object = pair->right->GetTESForm();
						}
					}
				}
			}
			return true;
		}
	}

	return false;
}

bool ProcessEventHandler(char *eventName, EventManager::EventCallback &callback, bool addEvt)
{
	if (GetLNEventMask)
	{
		char *colon = strchr(eventName, ':');
		if (colon)
			*colon++ = 0;
		UInt32 eventMask = GetLNEventMask(eventName);
		if (eventMask)
		{
			UInt32 numFilter = (colon && *colon) ? atoi(colon) : 0;
			return ProcessLNEventHandler(eventMask, callback.script, addEvt, callback.source, numFilter);
		}
	}
	return addEvt ? EventManager::SetHandler(eventName, callback) : EventManager::RemoveHandler(eventName, callback);
}

bool Cmd_SetEventHandler_Execute(COMMAND_ARGS)
{
	ExpressionEvaluator eval(PASS_COMMAND_ARGS);
	EventManager::EventCallback callback;
	char eventName[0x20];
	if (ExtractEventCallback(eval, &callback, eventName) && ProcessEventHandler(eventName, callback, true))
		*result = 1.0;

	return true;
}

bool Cmd_RemoveEventHandler_Execute(COMMAND_ARGS)
{
	ExpressionEvaluator eval(PASS_COMMAND_ARGS);
	EventManager::EventCallback callback;
	char eventName[0x20];
	if (ExtractEventCallback(eval, &callback, eventName) && ProcessEventHandler(eventName, callback, false))
		*result = 1.0;

	return true;
}

bool Cmd_GetCurrentEventName_Execute(COMMAND_ARGS)
{
	AssignToStringVar(PASS_COMMAND_ARGS, EventManager::GetCurrentEventName());
	return true;
}

bool Cmd_DispatchEvent_Execute(COMMAND_ARGS)
{
	ExpressionEvaluator eval(PASS_COMMAND_ARGS);
	if (!eval.ExtractArgs() || eval.NumArgs() == 0)
		return true;

	const char *eventName = eval.Arg(0)->GetString();
	if (!eventName)
		return true;

	ArrayID argsArrayId = 0;
	const char *senderName = NULL;
	if (eval.NumArgs() > 1)
	{
		if (!eval.Arg(1)->CanConvertTo(kTokenType_Array))
			return true;
		argsArrayId = eval.Arg(1)->GetArray();

		if (eval.NumArgs() > 2)
			senderName = eval.Arg(2)->GetString();
	}

	*result = EventManager::DispatchUserDefinedEvent(eventName, scriptObj, argsArrayId, senderName) ? 1.0 : 0.0;
	return true;
}

extern float g_gameSecondsPassed;

std::list<DelayedCallInfo> g_callAfterInfos;
ICriticalSection g_callAfterInfosCS;

bool Cmd_CallAfterSeconds_Execute(COMMAND_ARGS)
{
	float time;
	Script *callFunction;
	if (!ExtractArgs(EXTRACT_ARGS, &time, &callFunction) || !callFunction || !IS_ID(callFunction, Script))
		return true;
	ScopedLock lock(g_callAfterInfosCS);
	g_callAfterInfos.emplace_back(callFunction, g_gameSecondsPassed + time, thisObj);
	return true;
}

std::list<CallWhileInfo> g_callWhileInfos;
ICriticalSection g_callWhileInfosCS;

bool Cmd_CallWhile_Execute(COMMAND_ARGS)
{
	Script *callFunction;
	Script *conditionFunction;
	if (!ExtractArgs(EXTRACT_ARGS, &callFunction, &conditionFunction))
		return true;
	for (auto *form : {callFunction, conditionFunction})
		if (!form || !IS_ID(form, Script))
			return true;

	ScopedLock lock(g_callWhileInfosCS);
	g_callWhileInfos.emplace_back(callFunction, conditionFunction, thisObj);
	return true;
}

std::list<DelayedCallInfo> g_callForInfos;
ICriticalSection g_callForInfosCS;

bool Cmd_CallForSeconds_Execute(COMMAND_ARGS)
{
	float time;
	Script *callFunction;
	if (!ExtractArgs(EXTRACT_ARGS, &time, &callFunction) || !callFunction || !IS_ID(callFunction, Script))
		return true;
	ScopedLock lock(g_callAfterInfosCS);
	g_callForInfos.emplace_back(callFunction, g_gameSecondsPassed + time, thisObj);
	return true;
}

std::list<CallWhileInfo> g_callWhenInfos;
ICriticalSection g_callWhenInfosCS;

bool Cmd_CallWhen_Execute(COMMAND_ARGS)
{
	Script* callFunction;
	Script* conditionFunction;
	if (!ExtractArgs(EXTRACT_ARGS, &callFunction, &conditionFunction))
		return true;
	for (auto* form : { callFunction, conditionFunction })
		if (!form || !IS_ID(form, Script))
			return true;

	ScopedLock lock(g_callWhenInfosCS);
	g_callWhenInfos.emplace_back(callFunction, conditionFunction, thisObj);
	return true;

}

void DecompileScriptToFolder(const std::string& scriptName, Script* script, const std::string& fileExtension, const std::string_view& modName)
{
	ScriptParsing::ScriptAnalyzer analyzer(script);
	const auto* dirName = "DecompiledScripts";
	if (!std::filesystem::exists(dirName))
		std::filesystem::create_directory(dirName);
	const auto modDirName = FormatString("%s/%s", dirName, modName.data());
	if (!std::filesystem::exists(modDirName))
		std::filesystem::create_directory(modDirName);
	const auto filePath = modDirName + '/' + scriptName + '.' + fileExtension;
	std::ofstream os(filePath);
	os << analyzer.DecompileScript();
	if (IsConsoleMode())
		Console_Print("Decompiled script to '%s'", filePath.c_str());
}

bool Cmd_DecompileScript_Execute(COMMAND_ARGS)
{
	TESForm* form;
	*result = 0;
	char fileExtensionArg[0x100]{};
	if (!ExtractArgs(EXTRACT_ARGS, &form, &fileExtensionArg))
		return true;
	std::string fileExtension;
	if (fileExtensionArg[0])
		fileExtension = std::string(fileExtensionArg);
	else
		fileExtension = "gek";
	if (IS_ID(form, Script))
	{
		auto* script = static_cast<Script*>(form);
		std::string name = script->GetName();
		if (name.empty())
			name = FormatString("%X", script->refID & 0x00FFFFFF);
		DecompileScriptToFolder(name, script, fileExtension, GetModName(script));
	}
	else if (IS_ID(form, TESPackage))
	{
		auto* package = static_cast<TESPackage*>(form);
		for (auto& packageEvent : {std::make_pair("OnBegin", &package->onBeginAction), std::make_pair("OnEnd", &package->onEndAction), std::make_pair("OnChange", &package->onChangeAction)})
		{
			auto& [name, action] = packageEvent;
			if (action->script)
				DecompileScriptToFolder(std::string(package->GetName()) + name, action->script, fileExtension, GetModName(package));
		}
	}
	else
		return true;
	*result = 1;
	return true;
}

bool Cmd_HasScriptCommand_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32 commandOpcode;
	TESForm* form;
	UInt32 eventOpcode = -1;
	Script* script = nullptr;
	if (!ExtractArgs(EXTRACT_ARGS, &commandOpcode, &form, &eventOpcode))
		return true;
	if (!form)
		form = thisObj;
	if (!form)
		return true;
	if (IS_ID(form, Script))
		script = static_cast<Script*>(form);
	else if (form->GetIsReference())
	{
		auto* ref = static_cast<TESObjectREFR*>(form);
		if (auto* extraScript = ref->GetExtraScript())
			script = extraScript->script;
	}
	if (!script)
		return true;
	auto* cmdInfo = g_scriptCommands.GetByOpcode(commandOpcode);
	if (!cmdInfo)
		return true;
	CommandInfo* eventCmd = nullptr;
	if (eventOpcode != -1)
		eventCmd = GetEventCommandInfo(eventOpcode);
	if (ScriptParsing::ScriptContainsCommand(script, cmdInfo, eventCmd))
		*result = 1;
	return true;
}

static MemoizedMap<const char*, UInt32> s_opcodeMap;

bool Cmd_GetCommandOpcode_Execute(COMMAND_ARGS)
{
	*result = 0;
	char buf[0x400];
	if (!ExtractArgs(EXTRACT_ARGS, buf))
		return true;
	*result = s_opcodeMap.Get(buf, [](const char* buf)
	{
		auto* cmd = g_scriptCommands.GetByName(buf);
		if (!cmd)
			return 0u;
		return static_cast<unsigned>(cmd->opcode);
	});
	return true;
}

bool Cmd_Ternary_Execute(COMMAND_ARGS)
{
	*result = 0;
	if (ExpressionEvaluator eval(PASS_COMMAND_ARGS);
		eval.ExtractArgs())
	{
		ScriptToken* value = eval.Arg(0)->ToBasicToken();
		if (!value)
			return true;	// should never happen, could cause weird behavior otherwise.

		Script* call_udf = nullptr;
		if (value->GetBool()) {
			call_udf = eval.Arg(1)->GetUserFunction();
		}
		else {
			call_udf = eval.Arg(2)->GetUserFunction();
		}
		if (!call_udf)
			return true;
		
		InternalFunctionCaller caller(call_udf, thisObj, containingObj);
		caller.SetArgs(0);
		if (auto const tokenValResult = std::unique_ptr<ScriptToken>(UserFunctionManager::Call(std::move(caller))))
			tokenValResult->AssignResult(PASS_COMMAND_ARGS, eval);
	}
	return true;

}

#endif
