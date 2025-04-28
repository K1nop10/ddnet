#include <base/log.h>

#include <engine/console.h>
#include <engine/external/json-parser/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/localization.h>

#include <game/client/components/touch_controls.h>
#include <game/client/gameclient.h>

// Stores action related predefined behaviors.

// Used for switching actions.
int CTouchControls::NextActiveAction(int Action) const
{
	switch(Action)
	{
	case ACTION_FIRE:
		return ACTION_HOOK;
	case ACTION_HOOK:
		return ACTION_FIRE;
	default:
		dbg_assert(false, "Action invalid for NextActiveAction");
		return NUM_ACTIONS;
	}
}

int CTouchControls::NextDirectTouchAction() const
{
	if(m_pClient->m_Snap.m_SpecInfo.m_Active)
	{
		switch(m_DirectTouchSpectate)
		{
		case EDirectTouchSpectateMode::DISABLED:
			return NUM_ACTIONS;
		case EDirectTouchSpectateMode::AIM:
			return ACTION_AIM;
		default:
			dbg_assert(false, "m_DirectTouchSpectate invalid");
			return NUM_ACTIONS;
		}
	}
	else
	{
		switch(m_DirectTouchIngame)
		{
		case EDirectTouchIngameMode::DISABLED:
			return NUM_ACTIONS;
		case EDirectTouchIngameMode::ACTION:
			return m_ActionSelected;
		case EDirectTouchIngameMode::AIM:
			return ACTION_AIM;
		case EDirectTouchIngameMode::FIRE:
			return ACTION_FIRE;
		case EDirectTouchIngameMode::HOOK:
			return ACTION_HOOK;
		default:
			dbg_assert(false, "m_DirectTouchIngame invalid");
			return NUM_ACTIONS;
		}
	}
}

// Swap action button:
// - If joystick is currently active with one action: activate the other action.
// - Else: swap active action.
CTouchControls::CButtonLabel CTouchControls::CSwapActionTouchButtonBehavior::GetLabel() const
{
	if(m_ActiveAction != NUM_ACTIONS)
	{
		return {CButtonLabel::EType::LOCALIZED, ACTION_NAMES[m_ActiveAction]};
	}
	else if(m_pTouchControls->m_JoystickCount != 0)
	{
		return {CButtonLabel::EType::LOCALIZED, ACTION_NAMES[m_pTouchControls->NextActiveAction(m_pTouchControls->m_ActionSelected)]};
	}
	return {CButtonLabel::EType::LOCALIZED, ACTION_SWAP_NAMES[m_pTouchControls->m_ActionSelected]};
}

void CTouchControls::CSwapActionTouchButtonBehavior::OnActivate()
{
	if(m_pTouchControls->m_JoystickCount != 0)
	{
		m_ActiveAction = m_pTouchControls->NextActiveAction(m_pTouchControls->m_ActionSelected);
		m_pTouchControls->Console()->ExecuteLineStroked(1, ACTION_COMMANDS[m_ActiveAction]);
	}
	else
	{
		m_pTouchControls->m_ActionSelected = m_pTouchControls->NextActiveAction(m_pTouchControls->m_ActionSelected);
	}
}

void CTouchControls::CSwapActionTouchButtonBehavior::OnDeactivate()
{
	if(m_ActiveAction != NUM_ACTIONS)
	{
		m_pTouchControls->Console()->ExecuteLineStroked(0, ACTION_COMMANDS[m_ActiveAction]);
		m_ActiveAction = NUM_ACTIONS;
	}
}

// Use action button: always uses the active action.
CTouchControls::CButtonLabel CTouchControls::CUseActionTouchButtonBehavior::GetLabel() const
{
	if(m_ActiveAction != NUM_ACTIONS)
	{
		return {CButtonLabel::EType::LOCALIZED, ACTION_NAMES[m_ActiveAction]};
	}
	return {CButtonLabel::EType::LOCALIZED, ACTION_NAMES[m_pTouchControls->m_ActionSelected]};
}

void CTouchControls::CUseActionTouchButtonBehavior::OnActivate()
{
	m_ActiveAction = m_pTouchControls->m_ActionSelected;
	m_pTouchControls->Console()->ExecuteLineStroked(1, ACTION_COMMANDS[m_ActiveAction]);
}

void CTouchControls::CUseActionTouchButtonBehavior::OnDeactivate()
{
	m_pTouchControls->Console()->ExecuteLineStroked(0, ACTION_COMMANDS[m_ActiveAction]);
	m_ActiveAction = NUM_ACTIONS;
}

// Generic joystick button behavior: aim with virtual joystick and use action (defined by subclass).
CTouchControls::CButtonLabel CTouchControls::CJoystickTouchButtonBehavior::GetLabel() const
{
	if(m_ActiveAction != NUM_ACTIONS)
	{
		return {CButtonLabel::EType::LOCALIZED, ACTION_NAMES[m_ActiveAction]};
	}
	return {CButtonLabel::EType::LOCALIZED, ACTION_NAMES[SelectedAction()]};
}

void CTouchControls::CJoystickTouchButtonBehavior::OnActivate()
{
	m_ActiveAction = SelectedAction();
	OnUpdate();
	if(m_ActiveAction != ACTION_AIM)
	{
		m_pTouchControls->Console()->ExecuteLineStroked(1, ACTION_COMMANDS[m_ActiveAction]);
	}
	m_pTouchControls->m_JoystickCount++;
}

void CTouchControls::CJoystickTouchButtonBehavior::OnDeactivate()
{
	if(m_ActiveAction != ACTION_AIM)
	{
		m_pTouchControls->Console()->ExecuteLineStroked(0, ACTION_COMMANDS[m_ActiveAction]);
	}
	m_ActiveAction = NUM_ACTIONS;
	m_pTouchControls->m_JoystickCount--;
}

void CTouchControls::CJoystickTouchButtonBehavior::OnUpdate()
{
	CControls &Controls = m_pTouchControls->GameClient()->m_Controls;
	if(m_pTouchControls->GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		vec2 WorldScreenSize;
		m_pTouchControls->RenderTools()->CalcScreenParams(m_pTouchControls->Graphics()->ScreenAspect(), m_pTouchControls->GameClient()->m_Camera.m_Zoom, &WorldScreenSize.x, &WorldScreenSize.y);
		Controls.m_aMousePos[g_Config.m_ClDummy] += -m_AccumulatedDelta * WorldScreenSize;
		Controls.m_aMousePos[g_Config.m_ClDummy].x = clamp(Controls.m_aMousePos[g_Config.m_ClDummy].x, -201.0f * 32, (m_pTouchControls->Collision()->GetWidth() + 201.0f) * 32.0f);
		Controls.m_aMousePos[g_Config.m_ClDummy].y = clamp(Controls.m_aMousePos[g_Config.m_ClDummy].y, -201.0f * 32, (m_pTouchControls->Collision()->GetHeight() + 201.0f) * 32.0f);
		m_AccumulatedDelta = vec2(0.0f, 0.0f);
	}
	else
	{
		const vec2 AbsolutePosition = (m_ActivePosition - vec2(0.5f, 0.5f)) * 2.0f;
		Controls.m_aMousePos[g_Config.m_ClDummy] = AbsolutePosition * (Controls.GetMaxMouseDistance() - Controls.GetMinMouseDistance()) + normalize(AbsolutePosition) * Controls.GetMinMouseDistance();
		if(length(Controls.m_aMousePos[g_Config.m_ClDummy]) < 0.001f)
		{
			Controls.m_aMousePos[g_Config.m_ClDummy].x = 0.001f;
			Controls.m_aMousePos[g_Config.m_ClDummy].y = 0.0f;
		}
	}
}

// Joystick that uses the active action.
void CTouchControls::CJoystickActionTouchButtonBehavior::Init(CTouchButton *pTouchButton)
{
	CPredefinedTouchButtonBehavior::Init(pTouchButton);
}

int CTouchControls::CJoystickActionTouchButtonBehavior::SelectedAction() const
{
	return m_pTouchControls->m_ActionSelected;
}

// Joystick that only aims.
int CTouchControls::CJoystickAimTouchButtonBehavior::SelectedAction() const
{
	return ACTION_AIM;
}

// Joystick that always uses fire.
int CTouchControls::CJoystickFireTouchButtonBehavior::SelectedAction() const
{
	return ACTION_FIRE;
}

// Joystick that always uses hook.
int CTouchControls::CJoystickHookTouchButtonBehavior::SelectedAction() const
{
	return ACTION_HOOK;
}
