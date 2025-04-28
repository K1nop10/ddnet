#include <base/log.h>

#include <engine/console.h>
#include <engine/external/json-parser/json.h>
#include <engine/shared/jsonwriter.h>

#include <game/client/components/touch_controls.h>

// Stores bind behavior.

// Bind button behavior that executes a command like a bind.
CTouchControls::CButtonLabel CTouchControls::CBindTouchButtonBehavior::GetLabel() const
{
	return {m_LabelType, m_Label.c_str()};
}

void CTouchControls::CBindTouchButtonBehavior::SetLabel(CButtonLabel Label)
{
	m_Label = Label.m_pLabel;
	m_LabelType = Label.m_Type;
}

void CTouchControls::CBindTouchButtonBehavior::OnActivate()
{
	m_pTouchControls->Console()->ExecuteLineStroked(1, m_Command.c_str());
	m_Repeating = false;
}

void CTouchControls::CBindTouchButtonBehavior::OnDeactivate()
{
	m_pTouchControls->Console()->ExecuteLineStroked(0, m_Command.c_str());
}

void CTouchControls::CBindTouchButtonBehavior::OnUpdate()
{
	const auto Now = time_get_nanoseconds();
	if(m_Repeating)
	{
		m_AccumulatedRepeatingTime += Now - m_LastUpdateTime;
		m_LastUpdateTime = Now;
		if(m_AccumulatedRepeatingTime >= BIND_REPEAT_RATE)
		{
			m_AccumulatedRepeatingTime -= BIND_REPEAT_RATE;
			m_pTouchControls->Console()->ExecuteLineStroked(1, m_Command.c_str());
		}
	}
	else if(Now - m_ActivationStartTime >= BIND_REPEAT_INITIAL_DELAY)
	{
		m_Repeating = true;
		m_LastUpdateTime = Now;
		m_AccumulatedRepeatingTime = 0ns;
	}
}

void CTouchControls::CBindTouchButtonBehavior::WriteToConfiguration(CJsonWriter *pWriter)
{
	pWriter->WriteAttribute("type");
	pWriter->WriteStrValue(BEHAVIOR_TYPE);

	pWriter->WriteAttribute("label");
	pWriter->WriteStrValue(m_Label.c_str());

	pWriter->WriteAttribute("label-type");
	pWriter->WriteStrValue(LABEL_TYPE_NAMES[(int)m_LabelType]);

	pWriter->WriteAttribute("command");
	pWriter->WriteStrValue(m_Command.c_str());
}

std::unique_ptr<CTouchControls::CBindTouchButtonBehavior> CTouchControls::ParseBindBehavior(const json_value *pBehaviorObject)
{
	const json_value &BehaviorObject = *pBehaviorObject;
	const json_value &Label = BehaviorObject["label"];
	if(Label.type != json_string)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'label' must specify a string", CBindTouchButtonBehavior::BEHAVIOR_TYPE);
		return nullptr;
	}

	const json_value &LabelType = BehaviorObject["label-type"];
	if(LabelType.type != json_string && LabelType.type != json_none)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'label-type' must specify a string", CBindTouchButtonBehavior::BEHAVIOR_TYPE);
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
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'label-type' specifies unknown value '%s'", CBindTouchButtonBehavior::BEHAVIOR_TYPE, LabelType.u.string.ptr);
		return {};
	}

	const json_value &Command = BehaviorObject["command"];
	if(Command.type != json_string)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'command' must specify a string", CBindTouchButtonBehavior::BEHAVIOR_TYPE);
		return nullptr;
	}

	return std::make_unique<CBindTouchButtonBehavior>(Label.u.string.ptr, ParsedLabelType, Command.u.string.ptr);
}
