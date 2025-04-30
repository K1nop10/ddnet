#include <base/log.h>

#include <game/client/components/touch_controls.h>
#include <game/client/gameclient.h>

// This is called when the checkbox "Edit touch controls" is selected, so virtual visibility could be set as the real visibility on entering.
void CTouchControls::ResetVirtualVisibilities()
{
	// Update virtual visibilities.
	for(int Visibility = (int)EButtonVisibility::INGAME; Visibility < (int)EButtonVisibility::NUM_VISIBILITIES; ++Visibility)
		m_aVirtualVisibilities[Visibility] = m_aVisibilityFunctions[Visibility].m_Function();
}

void CTouchControls::EditButtons(const std::vector<IInput::CTouchFingerState> &vTouchFingerStates)
{
	std::set<CUnitRect> vVisibleButtonRects;
	const vec2 ScreenSize = CalculateScreenSize();

	// Remove if the finger deleted has released.
	if(!m_vDeletedFingerState.empty())
	{
		const auto &Remove = std::remove_if(m_vDeletedFingerState.begin(), m_vDeletedFingerState.end(), [&vTouchFingerStates](auto &TargetState) {
			return std::none_of(vTouchFingerStates.begin(), vTouchFingerStates.end(), [&](const auto State) {
				return State.m_Finger == TargetState.m_Finger;
			});
		});
		m_vDeletedFingerState.erase(Remove, m_vDeletedFingerState.end());
	}
	// Delete fingers if they are press later. So they cant be the longpress finger.
	if(vTouchFingerStates.size() > 1)
		std::for_each(vTouchFingerStates.begin() + 1, vTouchFingerStates.end(), [&](const auto &State) {
			m_vDeletedFingerState.push_back(State);
		});

	// If released, and there is finger on screen, and the "first finger" is not deleted(new finger), then it can be a LongPress candidate.
	if(!vTouchFingerStates.empty() && !std::any_of(m_vDeletedFingerState.begin(), m_vDeletedFingerState.end(), [&vTouchFingerStates](const auto &State) {
		   return vTouchFingerStates[0].m_Finger == State.m_Finger;
	   }))
	{
		// If has different finger, reset the accumulated delta.
		if(m_LongPressFingerState.has_value() && (*m_LongPressFingerState).m_Finger != vTouchFingerStates[0].m_Finger)
			m_AccumulatedDelta = {0.0f, 0.0f};
		// Update the LongPress candidate state.
		m_LongPressFingerState = vTouchFingerStates[0];
	}
	// If no suitable finger for long press, then clear it.
	else
	{
		m_LongPressFingerState = std::nullopt;
	}

	// Find long press button. LongPress == true means the first fingerstate long pressed.
	if(m_LongPressFingerState.has_value())
	{
		m_AccumulatedDelta += (*m_LongPressFingerState).m_Delta;
		// If slided, then delete.
		if(std::abs(m_AccumulatedDelta.x) + std::abs(m_AccumulatedDelta.y) > 0.005)
		{
			m_AccumulatedDelta = {0.0f, 0.0f};
			m_vDeletedFingerState.push_back(*m_LongPressFingerState);
			m_LongPressFingerState = std::nullopt;
		}
		// Till now, this else contains: if the finger hasn't slided, have no fingers that remain pressed down when it pressed, hasn't been a longpress already, the candidate is always the first finger.
		else
		{
			const auto Now = time_get_nanoseconds();
			if(Now - (*m_LongPressFingerState).m_PressTime > 400ms)
			{
				m_LongPress = true;
				m_vDeletedFingerState.push_back(*m_LongPressFingerState);
				// LongPress will be used this frame for sure, so reset delta.
				m_AccumulatedDelta = {0.0f, 0.0f};
			}
		}
	}

	// Update active and zoom fingerstate. The first finger will be used for moving button.
	if(!vTouchFingerStates.empty())
		m_ActiveFingerState = vTouchFingerStates[0];
	else
	{
		m_ActiveFingerState = std::nullopt;
		if(m_pTmpButton != nullptr && m_ShownRect.has_value())
		{
			m_pTmpButton->m_UnitRect = (*m_ShownRect);
		}
	}
	// Only the second finger will be used for zooming button.
	if(vTouchFingerStates.size() > 1)
	{
		// If zoom finger is pressed now, reset the zoom startpos
		if(!m_ZoomFingerState.has_value())
			m_ZoomStartPos = m_ActiveFingerState.value().m_Position - vTouchFingerStates[1].m_Position;
		m_ZoomFingerState = vTouchFingerStates[1];

		// If Zooming started, update it's x,y value so it's width and height could be calculated correctly.
		if(m_pTmpButton != nullptr && m_ShownRect.has_value())
		{
			m_pTmpButton->m_UnitRect.m_X = (*m_ShownRect).m_X;
			m_pTmpButton->m_UnitRect.m_Y = (*m_ShownRect).m_Y;
		}
	}
	else
	{
		m_ZoomFingerState = std::nullopt;
		m_ZoomStartPos = {0.0f, 0.0f};
		if(m_pTmpButton != nullptr && m_ShownRect.has_value())
		{
			m_pTmpButton->m_UnitRect.m_W = (*m_ShownRect).m_W;
			m_pTmpButton->m_UnitRect.m_H = (*m_ShownRect).m_H;
		}
	}
	for(auto &TouchButton : m_vTouchButtons)
	{
		bool IsVisible = std::all_of(TouchButton.m_vVisibilities.begin(), TouchButton.m_vVisibilities.end(), [&](const auto &Visibility) {
			return Visibility.m_Parity == m_aVirtualVisibilities[(int)Visibility.m_Type];
		});
		if(IsVisible)
		{
			if(m_pSelectedButton == &TouchButton)
				continue;
			// Only Long Pressed finger "in visible button" is used for selecting a button.
			if(m_LongPress && !vTouchFingerStates.empty() && TouchButton.IsInside((*m_LongPressFingerState).m_Position * ScreenSize))
			{
				// If m_pSelectedButton changes, Confirm if saving changes, then change.
				// LongPress used.
				m_LongPress = false;
				// Note: Even after the popup is opened by ChangeSelectedButtonWhile..., the fingerstate still exists. So we have to add it to m_vDeletedFingerState.
				m_vDeletedFingerState.push_back(*m_LongPressFingerState);
				m_LongPressFingerState = std::nullopt;
				if(m_UnsavedChanges)
				{
					m_PopupParam.m_KeepMenuOpen = false;
					m_PopupParam.m_OldSelectedButton = m_pSelectedButton;
					m_PopupParam.m_NewSelectedButton = &TouchButton;
					m_PopupParam.m_PopupType = EPopupType::BUTTON_CHANGED;
					GameClient()->m_Menus.SetActive(true);
					// End the function.
					return;
				}
				m_pSelectedButton = &TouchButton;
				// Update illegal position when Long press the button. Or later it will keep saying unsavedchanges.
				if(IfOverlapping(TouchButton.m_UnitRect))
				{
					TouchButton.m_UnitRect = UpdatePosition(TouchButton.m_UnitRect, false);
					if(TouchButton.m_UnitRect.m_X == -1)
					{
						m_PopupParam.m_PopupType = EPopupType::NO_SPACE;
						m_PopupParam.m_KeepMenuOpen = true;
						GameClient()->m_Menus.SetActive(true);
						return;
					}
					TouchButton.UpdateScreenFromUnitRect();
				}
				m_IssueParam[(int)EIssueType::CACHE_SETTINGS].m_pTargetButton = m_pSelectedButton;
				m_IssueParam[(int)EIssueType::CACHE_SETTINGS].m_Finished = false;
				m_pTmpButton = std::make_unique<CTouchButton>(this);
				CopySettings(m_pTmpButton.get(), m_pSelectedButton);
				log_error("bcd", "%s", m_IssueParam[0].m_pTargetButton->m_pBehavior->GetBehaviorType());
				// Don't insert the long pressed button. It is selected button now.
				continue;
			}
			// Insert visible but not selected buttons.
			vVisibleButtonRects.insert(TouchButton.m_UnitRect);
		}
		// If selected button not visible, unselect it.
		else if(m_pSelectedButton == &TouchButton && !GameClient()->m_Menus.IsActive())
		{
			m_PopupParam.m_PopupType = EPopupType::BUTTON_INVISIBLE;
			GameClient()->m_Menus.SetActive(true);
			return;
		}
	}
	// If LongPress == true, LongPress finger has to be outside of all visible buttons.(Except m_pTmpButton. This button hasn't been checked)
	if(m_LongPress)
	{
		m_LongPress = false;
		bool IsInside = CalculateScreenFromUnitRect(*m_ShownRect).Inside(m_LongPressFingerState->m_Position * ScreenSize);
		m_vDeletedFingerState.push_back(*m_LongPressFingerState);
		m_LongPressFingerState = std::nullopt;
		if(m_UnsavedChanges && !IsInside)
		{
			if(m_pSelectedButton != nullptr)
			{
				m_PopupParam.m_OldSelectedButton = m_pSelectedButton;
				m_PopupParam.m_NewSelectedButton = nullptr;
				m_PopupParam.m_KeepMenuOpen = false;
				m_PopupParam.m_PopupType = EPopupType::BUTTON_CHANGED;
				GameClient()->m_Menus.SetActive(true);
			}
			else if(m_pTmpButton != nullptr)
			{
				// First nullptr: Save the cached settings to "nullptr", a new button will be created automatically.
				// Second nullptr: Reset all cached settings to default.
				m_PopupParam.m_NewSelectedButton = nullptr;
				m_PopupParam.m_OldSelectedButton = nullptr;
				m_PopupParam.m_KeepMenuOpen = false;
				m_PopupParam.m_PopupType = EPopupType::BUTTON_CHANGED;
				GameClient()->m_Menus.SetActive(true);
			}
		}
		else if(!IsInside)
		{
			m_UnsavedChanges = false;
			ResetButtonPointers();
			// No need for caching settings issue. So the issue is set to finished.
			m_IssueParam[(int)EIssueType::CACHE_SETTINGS].m_Finished = true;
			m_IssueParam[(int)EIssueType::SAVE_SETTINGS].m_Finished = true;
		}
	}

	if(m_pTmpButton != nullptr)
	{
		if(m_ActiveFingerState.has_value() && m_ZoomFingerState == std::nullopt)
		{
			vec2 UnitXYDelta = m_ActiveFingerState->m_Delta * BUTTON_SIZE_SCALE;
			m_pTmpButton->m_UnitRect.m_X += UnitXYDelta.x;
			m_pTmpButton->m_UnitRect.m_Y += UnitXYDelta.y;
			m_ShownRect = FindPositionXY(vVisibleButtonRects, m_pTmpButton->m_UnitRect);
			if(m_pSelectedButton != nullptr)
			{
				unsigned Movement = std::abs(m_pSelectedButton->m_UnitRect.m_X - m_ShownRect->m_X) + std::abs(m_pSelectedButton->m_UnitRect.m_Y - m_ShownRect->m_Y);
				if(Movement > 10000)
				{
					// Moved a lot, meaning changes made.
					m_UnsavedChanges = true;
				}
			}
		}
		else if(m_ActiveFingerState.has_value() && m_ZoomFingerState.has_value())
		{
			m_ShownRect = m_pTmpButton->m_UnitRect;
			vec2 UnitWHDelta;
			UnitWHDelta.x = (std::abs(m_ActiveFingerState.value().m_Position.x - m_ZoomFingerState.value().m_Position.x) - std::abs(m_ZoomStartPos.x)) * BUTTON_SIZE_SCALE;
			UnitWHDelta.y = (std::abs(m_ActiveFingerState.value().m_Position.y - m_ZoomFingerState.value().m_Position.y) - std::abs(m_ZoomStartPos.y)) * BUTTON_SIZE_SCALE;
			(*m_ShownRect).m_W = m_pTmpButton->m_UnitRect.m_W + UnitWHDelta.x;
			(*m_ShownRect).m_H = m_pTmpButton->m_UnitRect.m_H + UnitWHDelta.y;
			(*m_ShownRect).m_W = clamp((*m_ShownRect).m_W, BUTTON_SIZE_MINIMUM, BUTTON_SIZE_MAXIMUM);
			(*m_ShownRect).m_H = clamp((*m_ShownRect).m_H, BUTTON_SIZE_MINIMUM, BUTTON_SIZE_MAXIMUM);
			if((*m_ShownRect).m_W + (*m_ShownRect).m_X > BUTTON_SIZE_SCALE)
				(*m_ShownRect).m_W = BUTTON_SIZE_SCALE - (*m_ShownRect).m_X;
			if((*m_ShownRect).m_H + (*m_ShownRect).m_Y > BUTTON_SIZE_SCALE)
				(*m_ShownRect).m_H = BUTTON_SIZE_SCALE - (*m_ShownRect).m_Y;
			// Clamp the biggest W and H so they won't overlap with other buttons. Known as "FindPositionWH".
			std::optional<int> BiggestW;
			std::optional<int> BiggestH;
			std::optional<int> LimitH, LimitW;
			for(const auto &Rect : vVisibleButtonRects)
			{
				// If Overlap
				if(!(Rect.m_X + Rect.m_W <= (*m_ShownRect).m_X || (*m_ShownRect).m_X + (*m_ShownRect).m_W <= Rect.m_X || Rect.m_Y + Rect.m_H <= (*m_ShownRect).m_Y || (*m_ShownRect).m_Y + (*m_ShownRect).m_H <= Rect.m_Y))
				{
					// Calculate the biggest Height and Width it could have.
					LimitH = Rect.m_Y - (*m_ShownRect).m_Y;
					LimitW = Rect.m_X - (*m_ShownRect).m_X;
					if(LimitH < BUTTON_SIZE_MINIMUM)
						LimitH = std::nullopt;
					if(LimitW < BUTTON_SIZE_MINIMUM)
						LimitW = std::nullopt;
					if(LimitH.has_value() && LimitW.has_value())
					{
						if(std::abs(*LimitH - (*m_ShownRect).m_H) < std::abs(*LimitW - (*m_ShownRect).m_W))
						{
							BiggestH = std::min(*LimitH, BiggestH.value_or(BUTTON_SIZE_SCALE));
						}
						else
						{
							BiggestW = std::min(*LimitW, BiggestW.value_or(BUTTON_SIZE_SCALE));
						}
					}
					else
					{
						if(LimitH.has_value())
							BiggestH = std::min(*LimitH, BiggestH.value_or(BUTTON_SIZE_SCALE));
						else if(LimitW.has_value())
							BiggestW = std::min(*LimitW, BiggestW.value_or(BUTTON_SIZE_SCALE));
						else
						{
							/*
							 * LimitH and W can be nullopt at the same time, because two buttons may be overlapping.
							 * Holding for long press while another finger is pressed.
							 * Then it will instantly enter zoom mode while buttons are overlapping with each other.
							 */
							m_ShownRect = FindPositionXY(vVisibleButtonRects, m_pTmpButton->m_UnitRect);
							BiggestW = std::nullopt;
							BiggestH = std::nullopt;
							break;
						}
					}
				}
			}
			(*m_ShownRect).m_W = BiggestW.value_or((*m_ShownRect).m_W);
			(*m_ShownRect).m_H = BiggestH.value_or((*m_ShownRect).m_H);
			m_UnsavedChanges = true;
		}
		// No finger on screen, then show it as is.
		else
		{
			m_ShownRect = m_pTmpButton->m_UnitRect;
		}
		// Finished moving, no finger on screen.
		if(vTouchFingerStates.empty())
		{
			m_AccumulatedDelta = {0.0f, 0.0f};
			m_ShownRect = FindPositionXY(vVisibleButtonRects, m_pTmpButton->m_UnitRect);
			m_pTmpButton->m_UnitRect = (*m_ShownRect);
			if(!GameClient()->m_Menus.IsActive())
			{
				m_IssueParam[(int)EIssueType::CACHE_POS].m_pTargetButton = m_pTmpButton.get();
				m_IssueParam[(int)EIssueType::CACHE_POS].m_Finished = false;
			}
			m_pTmpButton->UpdateScreenFromUnitRect();
		}
		if(m_ShownRect->m_X == -1)
		{
			m_PopupParam.m_PopupType = EPopupType::NO_SPACE;
			m_PopupParam.m_KeepMenuOpen = true;
			GameClient()->m_Menus.SetActive(true);
			return;
		}
		m_pTmpButton->UpdateScreenFromUnitRect();
	}
}

void CTouchControls::RenderButtonsWhileInEditor()
{
	for(auto &TouchButton : m_vTouchButtons)
	{
		if(&TouchButton == m_pSelectedButton)
			continue;
		bool IsVisible = std::all_of(TouchButton.m_vVisibilities.begin(), TouchButton.m_vVisibilities.end(), [&](const auto &Visibility) {
			return Visibility.m_Parity == m_aVirtualVisibilities[(int)Visibility.m_Type];
		});
		if(IsVisible || m_PreviewAllButtons)
		{
			TouchButton.UpdateScreenFromUnitRect();
			TouchButton.Render(false);
		}
	}

	if(m_pTmpButton != nullptr && m_ShownRect.has_value())
	{
		m_pTmpButton->Render(true, m_ShownRect);
	}
}

void CTouchControls::CQuadtreeNode::Split()
{
	m_NW = std::make_unique<CQuadtreeNode>(m_Space.m_X, m_Space.m_Y, m_Space.m_W / 2, m_Space.m_H / 2);
	m_NE = std::make_unique<CQuadtreeNode>(m_Space.m_X + m_Space.m_W / 2, m_Space.m_Y, m_Space.m_W / 2, m_Space.m_H / 2);
	m_SW = std::make_unique<CQuadtreeNode>(m_Space.m_X, m_Space.m_Y + m_Space.m_H / 2, m_Space.m_W / 2, m_Space.m_H / 2);
	m_SE = std::make_unique<CQuadtreeNode>(m_Space.m_X + m_Space.m_W / 2, m_Space.m_Y + m_Space.m_H / 2, m_Space.m_W / 2, m_Space.m_H / 2);
}

void CTouchControls::CQuadtree::Insert(CQuadtreeNode &Node, const CUnitRect &Rect, size_t Depth)
{
	if(Node.m_NW)
	{
		if(Node.m_NW->m_Space.IsOverlap(Rect))
			Insert(*Node.m_NW, Rect, Depth + 1);
		if(Node.m_NE->m_Space.IsOverlap(Rect))
			Insert(*Node.m_NE, Rect, Depth + 1);
		if(Node.m_SW->m_Space.IsOverlap(Rect))
			Insert(*Node.m_SW, Rect, Depth + 1);
		if(Node.m_SE->m_Space.IsOverlap(Rect))
			Insert(*Node.m_SE, Rect, Depth + 1);
		return;
	}
	Node.m_Rects.push_back(Rect);
	if(Node.m_Rects.size() > m_MaxObj && Depth < m_MaxDep)
	{
		Node.Split();
		for(const auto &TRect : Node.m_Rects)
		{
			Insert(Node, TRect, Depth);
		}
		Node.m_Rects.clear();
	}
}

bool CTouchControls::CQuadtree::Find(const CUnitRect &MyRect, CQuadtreeNode &Node)
{
	if(Node.m_NW)
	{
		if(MyRect.IsOverlap(Node.m_NE->m_Space) && Find(MyRect, *Node.m_NE))
			return true;
		if(MyRect.IsOverlap(Node.m_NW->m_Space) && Find(MyRect, *Node.m_NW))
			return true;
		if(MyRect.IsOverlap(Node.m_SE->m_Space) && Find(MyRect, *Node.m_SE))
			return true;
		if(MyRect.IsOverlap(Node.m_SW->m_Space) && Find(MyRect, *Node.m_SW))
			return true;
	}
	return std::any_of(Node.m_Rects.begin(), Node.m_Rects.end(), [&MyRect](const auto &Rect) {
		return MyRect.IsOverlap(Rect);
	});
}

CTouchControls::CUnitRect CTouchControls::FindPositionXY(const std::set<CUnitRect> &vVisibleButtonRects, CUnitRect MyRect)
{
	MyRect.m_X = clamp(MyRect.m_X, 0, BUTTON_SIZE_SCALE - MyRect.m_W);
	MyRect.m_Y = clamp(MyRect.m_Y, 0, BUTTON_SIZE_SCALE - MyRect.m_H);
	double TDis = BUTTON_SIZE_SCALE;
	CUnitRect TRec = {-1, -1, -1, -1};
	std::set<int> CandidateX;
	std::set<int> CandidateY;
	// o(N)
	bool IfOverlap = std::any_of(vVisibleButtonRects.begin(), vVisibleButtonRects.end(), [&MyRect](const auto &Rect) {
		return MyRect.IsOverlap(Rect);
	});
	if(!IfOverlap)
		return MyRect;
	// o(NlogN)
	for(const auto &Rect : vVisibleButtonRects)
	{
		int Pos = Rect.m_X + Rect.m_W;
		if(Pos + MyRect.m_W <= BUTTON_SIZE_SCALE)
			CandidateX.insert(Pos);
		Pos = Rect.m_X - MyRect.m_W;
		if(Pos >= 0)
			CandidateX.insert(Pos);
		Pos = Rect.m_Y + Rect.m_H;
		if(Pos + MyRect.m_H <= BUTTON_SIZE_SCALE)
			CandidateY.insert(Pos);
		Pos = Rect.m_Y - MyRect.m_H;
		if(Pos >= 0)
			CandidateY.insert(Pos);
	}
	CandidateX.insert(MyRect.m_X);
	CandidateY.insert(MyRect.m_Y);

	CQuadtree SearchTree(BUTTON_SIZE_SCALE, BUTTON_SIZE_SCALE);
	std::for_each(vVisibleButtonRects.begin(), vVisibleButtonRects.end(), [&SearchTree](const auto &Rect) {
		SearchTree.Insert(Rect);
	});
	for(const int &X : CandidateX)
		for(const int &Y : CandidateY)
		{
			CUnitRect TmpRect = {X, Y, MyRect.m_W, MyRect.m_H};
			if(!SearchTree.Find(TmpRect))
			{
				double Dis = TmpRect / MyRect;
				if(Dis < TDis)
				{
					TDis = Dis;
					TRec = TmpRect;
				}
			}
		}
	return TRec;
}

// Create a new button and push_back to m_vTouchButton, then return a pointer.
CTouchControls::CTouchButton *CTouchControls::NewButton()
{
	CTouchButton NewButton(this);
	NewButton.m_pBehavior = std::make_unique<CBindTouchButtonBehavior>("", CButtonLabel::EType::PLAIN, "");
	// So the vector's elements might be moved. If moved all button's m_VisibilityCached will be set to false. This should be prevented.
	std::vector<bool> CachedVisibilities;
	CachedVisibilities.reserve(m_vTouchButtons.size());
	for(const auto &Button : m_vTouchButtons)
	{
		CachedVisibilities.emplace_back(Button.m_VisibilityCached);
	}
	for(unsigned Iterator = 0; Iterator < CachedVisibilities.size(); Iterator++)
	{
		m_vTouchButtons[Iterator].m_VisibilityCached = CachedVisibilities[Iterator];
	}
	m_vTouchButtons.push_back(std::move(NewButton));
	return &m_vTouchButtons.back();
}

void CTouchControls::DeleteButton()
{
	if(m_pSelectedButton != nullptr)
	{
		auto DeleteIt = m_vTouchButtons.begin() + (m_pSelectedButton - m_vTouchButtons.data());
		m_vTouchButtons.erase(DeleteIt);
	}
	ResetButtonPointers();
	m_UnsavedChanges = false;
}

std::array<bool, CTouchControls::MAXNUMBER> CTouchControls::FindExistingExtraMenus()
{
	std::array<bool, CTouchControls::MAXNUMBER> Result;
	Result.fill(false);
	for(auto &TouchButton : m_vTouchButtons)
	{
		const char *PredefinedType = TouchButton.m_pBehavior->GetPredefinedType();
		if(PredefinedType == nullptr)
			continue;
		if(str_comp(PredefinedType, "extra-menu") == 0)
		{
			const auto &CastedBehavior = static_cast<CExtraMenuTouchButtonBehavior *>(TouchButton.m_pBehavior.get());
			Result[CastedBehavior->GetNumber()] = true;
		}
	}
	return Result;
}

bool CTouchControls::IfOverlapping(CUnitRect MyRect)
{
	for(auto &TouchButton : m_vTouchButtons)
	{
		if(m_pSelectedButton == &TouchButton)
			continue;
		bool IsVisible = std::all_of(TouchButton.m_vVisibilities.begin(), TouchButton.m_vVisibilities.end(), [&](const auto &Visibility) {
			return Visibility.m_Parity == m_aVirtualVisibilities[(int)Visibility.m_Type];
		});
		if(IsVisible && MyRect.IsOverlap(TouchButton.m_UnitRect))
			return true;
	}
	return false;
}

CTouchControls::CUnitRect CTouchControls::UpdatePosition(CUnitRect MyRect, bool Ignore)
{
	std::set<CUnitRect> vVisibleButtonRects;
	for(auto &TouchButton : m_vTouchButtons)
	{
		// If Ignore, Selected Button will also be added to the set.
		if(m_pSelectedButton == &TouchButton && !Ignore)
			continue;
		bool IsVisible = std::all_of(TouchButton.m_vVisibilities.begin(), TouchButton.m_vVisibilities.end(), [&](const auto &Visibility) {
			return Visibility.m_Parity == m_aVirtualVisibilities[(int)Visibility.m_Type];
		});
		if(IsVisible)
		{
			if(TouchButton.m_Shape == EButtonShape::RECT)
				vVisibleButtonRects.insert(TouchButton.m_UnitRect);
			else if(TouchButton.m_Shape == EButtonShape::CIRCLE)
			{
				CUnitRect Rect = TouchButton.m_UnitRect;
				if(Rect.m_H > Rect.m_W)
				{
					Rect.m_Y += (Rect.m_H - Rect.m_W) / 2;
					Rect.m_H = Rect.m_W;
				}
				else if(Rect.m_W > Rect.m_H)
				{
					Rect.m_X += (Rect.m_W - Rect.m_H) / 2;
					Rect.m_W = Rect.m_H;
				}
				vVisibleButtonRects.insert(Rect);
			}
		}
	}
	return FindPositionXY(vVisibleButtonRects, MyRect);
}

void CTouchControls::ResetButtonPointers()
{
	m_pSelectedButton = nullptr;
	m_pTmpButton = nullptr;
	m_ShownRect = std::nullopt;
}

// After sending the type, the popup should be reset immediately.
CTouchControls::CPopupParam CTouchControls::RequiredPopup()
{
	CPopupParam ReturnPopup = m_PopupParam;
	// Reset type so it won't be called for multiple times.
	m_PopupParam.m_PopupType = EPopupType::NUM_POPUPS;
	return ReturnPopup;
}

// Return true if any issue is not finished.
bool CTouchControls::IsIssueNotFinished()
{
	return std::any_of(m_IssueParam.begin(), m_IssueParam.end(), [](const auto &Issue) {
		return !Issue.m_Finished;
	});
}

std::array<CTouchControls::CIssueParam, (unsigned)CTouchControls::EIssueType::NUM_ISSUES> CTouchControls::Issues()
{
	std::array<CIssueParam, (unsigned)EIssueType::NUM_ISSUES> ReturnIssue;

	for(int Issue = 0; Issue < (int)EIssueType::NUM_ISSUES; Issue++)
	{
		ReturnIssue[Issue] = m_IssueParam[Issue];
		m_IssueParam[Issue].m_Finished = true;
	}
	return ReturnIssue;
}

// Make it look like the button, only have bind behavior. This is only used on m_pTmpButton.
void CTouchControls::CopySettings(CTouchButton *TargetButton, CTouchButton *SrcButton)
{
	if(TargetButton == nullptr || SrcButton == nullptr)
	{
		dbg_assert(false, "Failed to Copy settings to buttons. %s button is nullptr.", (TargetButton == nullptr) ? "Target" : "Source");
		return;
	}
	TargetButton->m_UnitRect = SrcButton->m_UnitRect;
	TargetButton->m_Shape = SrcButton->m_Shape;
	TargetButton->m_vVisibilities = SrcButton->m_vVisibilities;
	CButtonLabel Label = SrcButton->m_pBehavior->GetLabel();
	TargetButton->m_pBehavior = std::make_unique<CBindTouchButtonBehavior>(Label.m_pLabel, Label.m_Type, "");
	TargetButton->UpdatePointers();
	TargetButton->UpdateScreenFromUnitRect();
}

std::vector<CTouchControls::CTouchButton *> CTouchControls::VisibleButtons()
{
	std::vector<CTouchButton *> vReturnValue;
	std::for_each(m_vTouchButtons.begin(), m_vTouchButtons.end(), [&](auto &Button) {
		bool Visible = std::all_of(Button.m_vVisibilities.begin(), Button.m_vVisibilities.end(), [&](const CButtonVisibility &Visibility) {
			return Visibility.m_Parity == m_aVirtualVisibilities[(int)Visibility.m_Type];
		});
		if(Visible)
			vReturnValue.emplace_back(&Button);
	});
	return vReturnValue;
}

std::vector<CTouchControls::CTouchButton *> CTouchControls::InvisibleButtons()
{
	std::vector<CTouchButton *> vReturnValue;
	std::for_each(m_vTouchButtons.begin(), m_vTouchButtons.end(), [&](auto &Button) {
		bool Visible = std::all_of(Button.m_vVisibilities.begin(), Button.m_vVisibilities.end(), [&](const CButtonVisibility &Visibility) {
			return Visibility.m_Parity == m_aVirtualVisibilities[(int)Visibility.m_Type];
		});
		if(!Visible)
			vReturnValue.emplace_back(&Button);
	});
	return vReturnValue;
}
