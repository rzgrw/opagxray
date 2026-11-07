// Shared production implementation; included once by MainMenu.cpp.
// The focused test supplies lifecycle dependencies and executes these same methods.

bool CMainMenu::ReloadUI()
{
    if (m_startDialog)
    {
        if (m_startDialog->IsShown())
            m_startDialog->HideDialog();
        CleanInternals();
    }
    IFactoryObject* dlg = NEW_INSTANCE(xray::make_clsid("MAIN_MNU"));
    CUIDialogWnd* newDialog = dlg ? smart_cast<CUIDialogWnd*>(dlg) : nullptr;
    if (!newDialog)
    {
        Msg("! MAIN_MNU factory did not create a main menu dialog");
        // Keep the factory pointer until the cast succeeds so a rejected object
        // is destroyed through the same factory that created it.
        if (dlg)
            DEL_INSTANCE(dlg);
        xr_delete(m_startDialog);
        DeactivateUI();
        return false;
    }
    xr_delete(m_startDialog);

    m_startDialog = newDialog;
    m_startDialog->m_bWorkInPause = true;
    m_startDialog->ShowDialog(true);
    return true;
}

void CMainMenu::UpdateUIRestart()
{
    if (m_Flags.test(flNeedUIRestart))
    {
        m_Flags.set(flNeedUIRestart, false);
        if (ReloadUI())
            m_startDialog->SendMessage(m_startDialog, MAIN_MENU_RELOADED, NULL);
    }
}

void CMainMenu::Activate(bool bActivate)
{
    if (m_Flags.test(flActive) == bActivate)
        return;
    if (m_Flags.test(flGameSaveScreenshot))
        return;
    if ((m_screenshotFrame == Device.dwFrame) || (m_screenshotFrame == Device.dwFrame - 1) ||
        (m_screenshotFrame == Device.dwFrame + 1))
        return;

    bool b_is_single = IsGameTypeSingle();

    if (GEnv.isDedicatedServer && bActivate)
        return;

    if (bActivate)
    {
        b_shniaganeed_pp = true;
        m_activation = {};
        m_activation.started = true;
        // Pause can also change the pause label on its sound-only path.
        m_Flags.set(flRestorePauseStr, bShowPauseString);
        Device.Pause(TRUE, FALSE, TRUE, "mm_activate1");
        m_activation.soundPaused = !g_bBenchmark && GEnv.Sound;
        m_Flags.set(flActive | flNeedChangeCapture, TRUE);

        m_Flags.set(flRestoreCursor, GetUICursor().IsVisible());

        if (!ReloadUI())
            return;

        m_Flags.set(flRestoreConsole, Console->bVisible);
        if (b_is_single)
            m_Flags.set(flRestorePause, Device.Paused());

        Console->Hide();
        m_activation.consoleHidden = true;

        if (b_is_single)
        {
            bShowPauseString = FALSE;
            if (!m_Flags.test(flRestorePause))
            {
                Device.Pause(TRUE, TRUE, FALSE, "mm_activate2");
                m_activation.timerPaused = Device.Paused();
            }
        }

        if (g_pGameLevel)
        {
            m_activation.levelLifetimeId = g_pGameLevel->LifetimeId();
            m_activation.levelFrameRemoved = b_is_single && Device.seqFrame.Contains(g_pGameLevel);
            m_activation.levelRenderRemoved = Device.seqRender.Contains(g_pGameLevel);
            if (m_activation.levelFrameRemoved)
                Device.seqFrame.Remove(g_pGameLevel);
            if (m_activation.levelRenderRemoved)
                Device.seqRender.Remove(g_pGameLevel);
            CCameraManager::ResetPP();
        };
        Device.seqRender.Add(this, 4); // 1-console 2-cursor 3-tutorial
        m_activation.menuRenderAdded = true;

        Console->Execute("stat_memory");
    }
    else
    {
        DeactivateUI();

        /* Xottab_DUTY:

           Original code does Device.Reset() twice
           if we are in main menu and game level exist

           The first time is normal reset
           The second one happens when
           we are closing main menu

           Probable reason is that level needs to be precached

           I changed Mixed and Debug to do precache only
           instead of resetting entire Device

           XXX: find any problems that may happen
        */
        if (m_Flags.test(flNeedVidRestart))
        {
            m_Flags.set(flNeedVidRestart, FALSE);
#ifdef NDEBUG
            Device.Reset();
#else
            // Do only a precache for Debug and Mixed
            Device.PreCache(20, false);
#endif
        }
    }
}

// Failure must not go through Activate(false)'s screenshot/equality guards.
// Undo only effects acquired by this activation; the initial ReloadUI can fail
// before console, timer and registry changes have happened.
void CMainMenu::DeactivateUI()
{
    m_deactivated_frame = Device.dwFrame;
    m_Flags.set(flActive | flNeedChangeCapture | flNeedUIRestart, FALSE);

    if (m_activation.menuRenderAdded)
        Device.seqRender.Remove(this);

    const bool show = m_activation.consoleHidden && Console->bVisible;
    if (show)
        Console->Hide();
    IR_Release();
    if (show)
        Console->Show();

    if (m_startDialog && m_startDialog->IsShown())
        m_startDialog->HideDialog();
    CleanInternals();

    // Loading may replace/destroy the level (even reusing its address), or
    // register it again. Read identity only from the current live level.
    if (g_pGameLevel && m_activation.levelLifetimeId != 0 &&
        m_activation.levelLifetimeId == g_pGameLevel->LifetimeId() && g_pGameLevel->bReady)
    {
        if (m_activation.levelFrameRemoved && !Device.seqFrame.Contains(g_pGameLevel))
            Device.seqFrame.Add(g_pGameLevel);
        if (m_activation.levelRenderRemoved && !Device.seqRender.Contains(g_pGameLevel))
            Device.seqRender.Add(g_pGameLevel);
    }
    if (m_activation.consoleHidden && m_Flags.test(flRestoreConsole))
        Console->Show();

    if (m_activation.timerPaused)
        Device.Pause(FALSE, TRUE, FALSE, "mm_deactivate1");
    if (m_activation.started)
    {
        bShowPauseString = m_Flags.test(flRestorePauseStr);
        if (m_Flags.test(flRestoreCursor))
            GetUICursor().Show();
    }
    if (m_activation.soundPaused)
        Device.Pause(FALSE, FALSE, TRUE, "mm_deactivate2");
    m_activation = {};
}
