#include <base/log.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/external/json-parser/json.h>
#include <engine/shared/jsonwriter.h>

#include <game/client/components/touch_controls.h>
#include <memory>
#include <string>
#include <unordered_set>

// Stores the standard mixed behaviors.

void CTouchControls::CMixedTouchButtonBehavior::Init(CTouchControls::CTouchButton *pTouchButton)
{
	m_pTouchButton = pTouchButton;
	m_pTouchControls = pTouchButton->m_pTouchControls;
	for(const auto &pBehavior : m_vAllBehaviors)
	{
		pBehavior->m_pTouchButton = pTouchButton;
		pBehavior->m_pTouchControls = pTouchButton->m_pTouchControls;
	}
}

CTouchControls::CButtonLabel CTouchControls::CMixedTouchButtonBehavior::GetLabel() const
{
	return {m_LabelType, m_Label.c_str()};
}

void CTouchControls::CMixedTouchButtonBehavior::SetLabel(CTouchControls::CButtonLabel Label)
{
	m_Label = Label.m_pLabel;
	m_LabelType = Label.m_Type;
}

std::vector<CTouchControls::CTouchButtonBehavior *> CTouchControls::CMixedTouchButtonBehavior::GetBehaviors() const
{
	std::vector<CTouchControls::CTouchButtonBehavior *> AllBehaviors;
	AllBehaviors.reserve(m_vAllBehaviors.size());
	for(const auto &Behavior : m_vAllBehaviors)
		AllBehaviors.emplace_back(Behavior.get());
	return AllBehaviors;
}

void CTouchControls::CMixedTouchButtonBehavior::SetBehaviors(std::vector<std::unique_ptr<CTouchButtonBehavior>> AllBehaviors)
{
	m_vAllBehaviors.clear();
	m_vAllBehaviors.reserve(AllBehaviors.size());
	for(auto &Behavior : AllBehaviors)
	{
		m_vAllBehaviors.emplace_back(std::move(Behavior));
	}
}

void CTouchControls::CMixedTouchButtonBehavior::OnActivate()
{
	UpdateBehavior();
	for(const auto &pBehavior : m_vAllBehaviors)
	{
		pBehavior->OnActivate();
	}
}

void CTouchControls::CMixedTouchButtonBehavior::OnDeactivate()
{
	UpdateBehavior();
	for(const auto &pBehavior : m_vAllBehaviors)
	{
		pBehavior->OnDeactivate();
	}
}

void CTouchControls::CMixedTouchButtonBehavior::OnUpdate()
{
	UpdateBehavior();
	for(const auto &pBehavior : m_vAllBehaviors)
	{
		pBehavior->OnUpdate();
	}
}

void CTouchControls::CMixedTouchButtonBehavior::WriteToConfiguration(CJsonWriter *pWriter)
{
	pWriter->WriteAttribute("type");
	pWriter->WriteStrValue(BEHAVIOR_TYPE);

	pWriter->WriteAttribute("label");
	pWriter->WriteStrValue(m_Label.c_str());

	pWriter->WriteAttribute("label-type");
	pWriter->WriteStrValue(LABEL_TYPE_NAMES[(int)m_LabelType]);

	pWriter->WriteAttribute("behaviors");
	pWriter->BeginArray();

	for(const auto &pBehavior : m_vAllBehaviors)
	{
		pWriter->BeginObject();
		pBehavior->WriteToConfiguration(pWriter);
		pWriter->EndObject();
	}

	pWriter->EndArray();
}

std::unique_ptr<CTouchControls::CMixedTouchButtonBehavior> CTouchControls::ParseMixedBehavior(const json_value *pBehaviorObject)
{
	const json_value &BehaviorObject = *pBehaviorObject;
	const json_value &Behaviors = BehaviorObject["behaviors"];
	if(Behaviors.type != json_array)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'behaviors' must specify an array", CMixedTouchButtonBehavior::BEHAVIOR_TYPE);
		return nullptr;
	}
	if(Behaviors.u.array.length < 2)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'behaviors' is too short", CMixedTouchButtonBehavior::BEHAVIOR_TYPE);
		return nullptr;
	}

	std::string ParsedLabel;
	const json_value &Label = BehaviorObject["label"];
	if(Label.type == json_string)
	{
		ParsedLabel = Label.u.string.ptr;
	}
	else if(Label.type != json_none)
	{
		log_error("touch_controls", "Failed to parse mixed behavior: attribute \"label\" is not string");
		return nullptr;
	}

	CButtonLabel::EType ParsedLabelType = CButtonLabel::EType::PLAIN;
	const json_value &LabelType = BehaviorObject["label-type"];
	if(LabelType.type == json_string)
	{
		auto Target = m_LabelTypeFactory.find(LabelType.u.string.ptr);
		if(Target == m_LabelTypeFactory.end())
		{
			log_error("touch_controls", "Failed to parse mixed behavior: attribute \"label-type\" specifies an unknown value %s", LabelType.u.string.ptr);
			return nullptr;
		}
		ParsedLabelType = Target->second;
	}
	else if(LabelType.type != json_none)
	{
		log_error("touch_controls", "Failed to parse mixed behavior: attribute \"label-type\" is not string");
		return nullptr;
	}

	std::unordered_set<std::string> vParsedTypes;
	std::unordered_set<std::string> vParsedIds;
	std::vector<std::unique_ptr<CTouchButtonBehavior>> vParsedBehaviors;
	vParsedBehaviors.reserve(Behaviors.u.array.length);
	for(unsigned Index = 0; Index < Behaviors.u.array.length; Index++)
	{
		const json_value Behavior = Behaviors[Index];
		if(Behavior.type != json_object)
		{
			log_error("touch_controls", "Failed to parse mixed behavior: %d behavior is not an object", Index);
			return nullptr;
		}
		if(Behavior["type"].type != json_string)
		{
			log_error("touch_controls", "Failed to parse mixed behavior: the attribute \"type\" of %d behavior is not string", Index);
			return nullptr;
		}
		if(str_comp(Behavior["type"].u.string.ptr, CMixedTouchButtonBehavior::BEHAVIOR_TYPE) == 0)
		{
			log_error("touch_controls", "It's not allowed to use mixed in mixed.");
			return nullptr;
		}
		if(str_comp(Behavior["type"].u.string.ptr, CPredefinedTouchButtonBehavior::BEHAVIOR_TYPE) == 0)
		{
			if(Behavior["id"].type != json_string)
			{
				log_error("touch_controls", "Failed to parse mixed behavior: the attribute \"id\" of %d behavior is not string", Index);
				return nullptr;
			}
		}
		std::unique_ptr<CTouchButtonBehavior> ParsedBehavior = ParseBehavior(&Behavior);
		if(ParsedBehavior == nullptr)
		{
			log_error("touch_controls", "Mixing behaviors failed, errors occurred while parsing behaviors");
			return nullptr;
		}
		if(str_comp(ParsedBehavior->GetBehaviorType(), CPredefinedTouchButtonBehavior::BEHAVIOR_TYPE) == 0)
		{
			if(vParsedIds.find(ParsedBehavior->GetPredefinedType()) != vParsedIds.end())
			{
				log_error("touch_controls", "Duplicate predefined behaviors found while mixing");
				return nullptr;
			}
			vParsedIds.emplace(ParsedBehavior->GetPredefinedType());
		}
		else if(vParsedTypes.find(ParsedBehavior->GetBehaviorType()) != vParsedTypes.end())
		{
			log_error("touch_controls", "Duplicate standard behaviors found while mixing");
			return nullptr;
		}
		vParsedTypes.emplace(ParsedBehavior->GetBehaviorType());
		vParsedBehaviors.push_back(std::move(ParsedBehavior));
	}
	if(vParsedTypes.find(CBindTouchButtonBehavior::BEHAVIOR_TYPE) != vParsedTypes.end() &&
		vParsedTypes.find(CBindToggleTouchButtonBehavior::BEHAVIOR_TYPE) != vParsedTypes.end())
	{
		log_error("touch_controls", "Both bind and bind toggle found while mixing. Use one only");
		return nullptr;
	}
	unsigned short JoystickCount = 0;
	if(vParsedIds.find(CJoystickActionTouchButtonBehavior::BEHAVIOR_ID) != vParsedIds.end())
		JoystickCount++;
	if(vParsedIds.find(CJoystickAimTouchButtonBehavior::BEHAVIOR_ID) != vParsedIds.end())
		JoystickCount++;
	if(vParsedIds.find(CJoystickFireTouchButtonBehavior::BEHAVIOR_ID) != vParsedIds.end())
		JoystickCount++;
	if(vParsedIds.find(CJoystickHookTouchButtonBehavior::BEHAVIOR_ID) != vParsedIds.end())
		JoystickCount++;
	if(JoystickCount > 1)
	{
		log_error("touch_controls", "At most one joystick is allowed for mixing.");
		return nullptr;
	}
	return std::make_unique<CMixedTouchButtonBehavior>(ParsedLabel, ParsedLabelType, std::move(vParsedBehaviors));
}

void CTouchControls::CMixedTouchButtonBehavior::UpdateBehavior()
{
	for(std::unique_ptr<CTouchButtonBehavior> &pBehavior : m_vAllBehaviors)
	{
		pBehavior->m_Active = m_Active;
		pBehavior->m_Finger = m_Finger;
		pBehavior->m_ActivePosition = m_ActivePosition;
		pBehavior->m_AccumulatedDelta = m_AccumulatedDelta;
		pBehavior->m_ActivationStartTime = m_ActivationStartTime;
		pBehavior->m_Delta = m_Delta;
	}
}
