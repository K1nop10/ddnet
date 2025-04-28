#include <base/log.h>

#include <engine/console.h>
#include <engine/external/json-parser/json.h>
#include <engine/shared/jsonwriter.h>

#include <game/client/components/touch_controls.h>

// Stores bind toggle behavior.

// Bind button behavior that switches between executing one of two or more console commands.
CTouchControls::CButtonLabel CTouchControls::CBindToggleTouchButtonBehavior::GetLabel() const
{
	const auto &ActiveCommand = m_vCommands[m_ActiveCommandIndex];
	return {ActiveCommand.m_LabelType, ActiveCommand.m_Label.c_str()};
}

void CTouchControls::CBindToggleTouchButtonBehavior::OnActivate()
{
	m_pTouchControls->Console()->ExecuteLine(m_vCommands[m_ActiveCommandIndex].m_Command.c_str());
	m_ActiveCommandIndex = (m_ActiveCommandIndex + 1) % m_vCommands.size();
}

void CTouchControls::CBindToggleTouchButtonBehavior::WriteToConfiguration(CJsonWriter *pWriter)
{
	pWriter->WriteAttribute("type");
	pWriter->WriteStrValue(BEHAVIOR_TYPE);

	pWriter->WriteAttribute("commands");
	pWriter->BeginArray();

	for(const auto &Command : m_vCommands)
	{
		pWriter->BeginObject();

		pWriter->WriteAttribute("label");
		pWriter->WriteStrValue(Command.m_Label.c_str());

		pWriter->WriteAttribute("label-type");
		pWriter->WriteStrValue(LABEL_TYPE_NAMES[(int)Command.m_LabelType]);

		pWriter->WriteAttribute("command");
		pWriter->WriteStrValue(Command.m_Command.c_str());

		pWriter->EndObject();
	}

	pWriter->EndArray();
}

std::unique_ptr<CTouchControls::CBindToggleTouchButtonBehavior> CTouchControls::ParseBindToggleBehavior(const json_value *pBehaviorObject)
{
	const json_value &CommandsObject = (*pBehaviorObject)["commands"];
	if(CommandsObject.type != json_array || CommandsObject.u.array.length < 2)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'commands' must specify an array with at least 2 entries", CBindToggleTouchButtonBehavior::BEHAVIOR_TYPE);
		return {};
	}

	std::vector<CTouchControls::CBindToggleTouchButtonBehavior::CCommand> vCommands;
	vCommands.reserve(CommandsObject.u.array.length);
	for(unsigned CommandIndex = 0; CommandIndex < CommandsObject.u.array.length; ++CommandIndex)
	{
		const json_value &CommandObject = CommandsObject[CommandIndex];
		if(CommandObject.type != json_object)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'commands' must specify an array of objects", CBindToggleTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
			return nullptr;
		}

		const json_value &Label = CommandObject["label"];
		if(Label.type != json_string)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'label' must specify a string", CBindToggleTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
			return nullptr;
		}

		const json_value &LabelType = CommandObject["label-type"];
		if(LabelType.type != json_string && LabelType.type != json_none)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'label-type' must specify a string", CBindToggleTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
			return {};
		}
		CButtonLabel::EType ParsedLabelType = CButtonLabel::EType::NUM_TYPES;
		if(LabelType.type == json_none)
		{
			ParsedLabelType = CButtonLabel::EType::PLAIN;
		}
		else
		{
			for(int CurrentType = (int)CButtonLabel::EType::PLAIN; CurrentType < (int)CButtonLabel::EType::NUM_TYPES; ++CurrentType)
			{
				if(str_comp(LabelType.u.string.ptr, LABEL_TYPE_NAMES[CurrentType]) == 0)
				{
					ParsedLabelType = (CButtonLabel::EType)CurrentType;
					break;
				}
			}
		}
		if(ParsedLabelType == CButtonLabel::EType::NUM_TYPES)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'label-type' specifies unknown value '%s'", CBindToggleTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex, LabelType.u.string.ptr);
			return {};
		}

		const json_value &Command = CommandObject["command"];
		if(Command.type != json_string)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'command' must specify a string", CBindToggleTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
			return nullptr;
		}
		vCommands.emplace_back(Label.u.string.ptr, ParsedLabelType, Command.u.string.ptr);
	}
	return std::make_unique<CBindToggleTouchButtonBehavior>(std::move(vCommands));
}
