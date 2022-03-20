/*****************************************************************************
 * main_interface.cpp : Main interface
 ****************************************************************************
 * Copyright (C) 2006-2011 VideoLAN and AUTHORS
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Jean-Baptiste Kempf <jb@videolan.org>
 *          Ilkka Ollakka <ileoo@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include "maininterface/mainctx.hpp"
#include "compositor.hpp"
#include "util/renderer_manager.hpp"

#include "widgets/native/customwidgets.hpp"               // qtEventToVLCKey, QVLCStackedWidget
#include "util/qt_dirs.hpp"                     // toNativeSeparators

#include "widgets/native/interface_widgets.hpp"     // bgWidget, videoWidget

#include "playlist/playlist_controller.hpp"

#include "videosurface.hpp"

#include "menus/menus.hpp"                            // Menu creation

#include "dialogs/toolbar/controlbar_profile_model.hpp"

#include <QKeyEvent>

#include <QUrl>
#include <QDate>
#include <QMimeData>

#include <QWindow>
#include <QScreen>
#ifdef _WIN32
#include <QFileInfo>
#endif

#define VLC_REFERENCE_SCALE_FACTOR 96.

using  namespace vlc::playlist;

// #define DEBUG_INTF

/* Callback prototypes */
static int PopupMenuCB( vlc_object_t *p_this, const char *psz_variable,
                        vlc_value_t old_val, vlc_value_t new_val, void *param );
static int IntfShowCB( vlc_object_t *p_this, const char *psz_variable,
                       vlc_value_t old_val, vlc_value_t new_val, void *param );
static int IntfBossCB( vlc_object_t *p_this, const char *psz_variable,
                       vlc_value_t old_val, vlc_value_t new_val, void *param );
static int IntfRaiseMainCB( vlc_object_t *p_this, const char *psz_variable,
                           vlc_value_t old_val, vlc_value_t new_val,
                           void *param );

const QEvent::Type MainCtx::ToolbarsNeedRebuild =
        (QEvent::Type)QEvent::registerEventType();

namespace
{

template <typename T>
T loadVLCOption(vlc_object_t *obj, const char *name);

template <>
int loadVLCOption<int>(vlc_object_t *obj, const char *name)
{
    return var_InheritInteger(obj, name);
}

template <>
bool loadVLCOption<bool>(vlc_object_t *obj, const char *name)
{
    return var_InheritBool(obj, name);
}

}

MainCtx::MainCtx(qt_intf_t *_p_intf)
    : p_intf(_p_intf)
{
    /**
     *  Configuration and settings
     *  Pre-building of interface
     **/

    settings = getSettings();
    m_colorScheme = new ColorSchemeModel(this);

    loadPrefs(false);
    loadFromSettingsImpl(false);

    /* Get the available interfaces */
    m_extraInterfaces = new VLCVarChoiceModel(VLC_OBJECT(p_intf->intf), "intf-add", this);

    vlc_medialibrary_t* ml = vlc_ml_instance_get( p_intf );
    b_hasMedialibrary = (ml != NULL);
    if (b_hasMedialibrary) {
        m_medialib = new MediaLib(p_intf);
    }

    /* Controlbar Profile Model Creation */
    m_controlbarProfileModel = new ControlbarProfileModel(p_intf, this);

    m_dialogFilepath = getSettings()->value( "filedialog-path", QVLCUserDir( VLC_HOME_DIR ) ).toString();

    QString platformName = QGuiApplication::platformName();

#ifdef QT5_HAS_WAYLAND
    b_hasWayland = platformName.startsWith(QLatin1String("wayland"), Qt::CaseInsensitive);
#endif

    /*********************************
     * Create the Systray Management *
     *********************************/
    //postpone systray initialisation to speedup starting time
    QMetaObject::invokeMethod(this, &MainCtx::initSystray, Qt::QueuedConnection);

    /*************************************************************
     * Connect the input manager to the GUI elements it manages  *
     * Beware initSystray did some connects on input manager too *
     *************************************************************/
    /**
     * Connects on nameChanged()
     * Those connects are different because options can impeach them to trigger.
     **/
    /* Main Interface statusbar */
    /* and title of the Main Interface*/
    connect( THEMIM, &PlayerController::inputChanged, this, &MainCtx::onInputChanged );

    /* END CONNECTS ON IM */

    /* VideoWidget connects for asynchronous calls */
    connect( this, &MainCtx::askToQuit, THEDP, &DialogsProvider::quit, Qt::QueuedConnection  );

    QMetaObject::invokeMethod(this, [this]()
    {
        // *** HACKY ***
        assert(p_intf->p_compositor->interfaceMainWindow());
        connect(p_intf->p_compositor->interfaceMainWindow(), &QWindow::screenChanged,
                this, &MainCtx::screenChanged);
    }, Qt::QueuedConnection);

    /** END of CONNECTS**/


    /************
     * Callbacks
     ************/
    libvlc_int_t* libvlc = vlc_object_instance(p_intf);
    var_AddCallback( libvlc, "intf-toggle-fscontrol", IntfShowCB, p_intf );
    var_AddCallback( libvlc, "intf-boss", IntfBossCB, p_intf );
    var_AddCallback( libvlc, "intf-show", IntfRaiseMainCB, p_intf );

    /* Register callback for the intf-popupmenu variable */
    var_AddCallback( libvlc, "intf-popupmenu", PopupMenuCB, p_intf );

    if( config_GetInt("qt-privacy-ask") )
    {
        //postpone dialog call, as composition might not be ready yet
        QMetaObject::invokeMethod(this, [this](){
            THEDP->firstRunDialog();
        }, Qt::QueuedConnection);
    }
}

MainCtx::~MainCtx()
{
    RendererManager::killInstance();

    /* Save states */

    settings->beginGroup("MainWindow");
    settings->setValue( "pl-dock-status", b_playlistDocked );
    settings->setValue( "ShowRemainingTime", m_showRemainingTime );
    settings->setValue( "interface-scale", m_intfUserScaleFactor );
    settings->setValue( "pin-video-controls", m_pinVideoControls );

    /* Save playlist state */
    settings->setValue( "playlist-visible", playlistVisible );
    settings->setValue( "playlist-width-factor", playlistWidthFactor);

    settings->setValue( "grid-view", m_gridView );
    settings->setValue( "grouping", m_grouping );

    settings->setValue( "color-scheme", m_colorScheme->currentScheme() );
    /* Save the stackCentralW sizes */
    settings->endGroup();

    if( var_InheritBool( p_intf, "save-recentplay" ) )
        getSettings()->setValue( "filedialog-path", m_dialogFilepath );
    else
        getSettings()->remove( "filedialog-path" );

    /* Unregister callbacks */
    libvlc_int_t* libvlc = vlc_object_instance(p_intf);
    var_DelCallback( libvlc, "intf-boss", IntfBossCB, p_intf );
    var_DelCallback( libvlc, "intf-show", IntfRaiseMainCB, p_intf );
    var_DelCallback( libvlc, "intf-toggle-fscontrol", IntfShowCB, p_intf );
    var_DelCallback( libvlc, "intf-popupmenu", PopupMenuCB, p_intf );

    if (m_medialib)
        m_medialib->destroy();

    p_intf->p_mi = NULL;
}

bool MainCtx::hasVLM() const {
#ifdef ENABLE_VLM
    return true;
#else
    return false;
#endif
}

bool MainCtx::useClientSideDecoration() const
{
    //don't show CSD when interface is fullscreen
    return !m_windowTitlebar;
}

bool MainCtx::hasFirstrun() const {
    return config_GetInt( "qt-privacy-ask" );
}

/*****************************
 *   Main UI handling        *
 *****************************/

void MainCtx::loadPrefs(const bool callSignals)
{
    const auto loadFromVLCOption = [this, callSignals](auto &variable, const char *name
            , const std::function<void(MainCtx *)> signal)
    {
        using variableType = std::remove_reference_t<decltype(variable)>;

        const auto value =  loadVLCOption<variableType>(VLC_OBJECT(p_intf), name);
        if (value == variable)
            return;

        variable = value;
        if (callSignals && signal)
            signal(this);
    };

    /* Are we in the enhanced always-video mode or not ? */
    loadFromVLCOption(b_minimalView, "qt-minimal-view", nullptr);

    /* Do we want annoying popups or not */
    loadFromVLCOption(i_notificationSetting, "qt-notification", nullptr);

    /* Should the UI stays on top of other windows */
    loadFromVLCOption(b_interfaceOnTop, "video-on-top", [this](MainCtx *)
    {
        emit interfaceAlwaysOnTopChanged(b_interfaceOnTop);
    });

    loadFromVLCOption(m_hasToolbarMenu, "qt-menubar", &MainCtx::hasToolbarMenuChanged);

#if QT_CLIENT_SIDE_DECORATION_AVAILABLE
    loadFromVLCOption(m_windowTitlebar, "qt-titlebar" , &MainCtx::useClientSideDecorationChanged);
#endif

    loadFromVLCOption(m_smoothScroll, "qt-smooth-scrolling", &MainCtx::smoothScrollChanged);
}

void MainCtx::loadFromSettingsImpl(const bool callSignals)
{
    const auto loadFromSettings = [this, callSignals](auto &variable, const char *name
            , const auto defaultValue, auto signal)
    {
        using variableType = std::remove_reference_t<decltype(variable)>;

        const auto value = getSettings()->value(name, defaultValue).template value<variableType>();
        if (value == variable)
            return;

        variable = value;
        if (callSignals && signal)
            (this->*signal)(variable);
    };

    loadFromSettings(b_playlistDocked, "MainWindow/pl-dock-status", true, &MainCtx::playlistDockedChanged);

    loadFromSettings(playlistVisible, "MainWindow/playlist-visible", false, &MainCtx::playlistVisibleChanged);

    loadFromSettings(playlistWidthFactor, "MainWindow/playlist-width-factor", 4.0 , &MainCtx::playlistWidthFactorChanged);

    loadFromSettings(m_gridView, "MainWindow/grid-view", true, &MainCtx::gridViewChanged);

    loadFromSettings(m_grouping, "MainWindow/grouping", GROUPING_NONE, &MainCtx::groupingChanged);

    loadFromSettings(m_showRemainingTime, "MainWindow/ShowRemainingTime", false, &MainCtx::showRemainingTimeChanged);

    loadFromSettings(m_pinVideoControls, "MainWindow/pin-video-controls", false, &MainCtx::pinVideoControlsChanged);

    const auto colorScheme = static_cast<ColorSchemeModel::ColorScheme>(getSettings()->value( "MainWindow/color-scheme", ColorSchemeModel::System ).toInt());
    if (m_colorScheme->currentScheme() != colorScheme)
        m_colorScheme->setCurrentScheme(colorScheme);

    /* user interface scale factor */
    auto userIntfScaleFactor = var_InheritFloat(p_intf, "qt-interface-scale");
    if (userIntfScaleFactor == -1)
        userIntfScaleFactor = getSettings()->value( "MainWindow/interface-scale", 1.0).toDouble();
    if (m_intfUserScaleFactor != userIntfScaleFactor)
    {
        m_intfUserScaleFactor = userIntfScaleFactor;
        updateIntfScaleFactor();
    }
}

void MainCtx::reloadPrefs()
{
    loadPrefs(true);
}

void MainCtx::onInputChanged( bool hasInput )
{
    if( hasInput == false )
        return;
    int autoRaise = var_InheritInteger( p_intf, "qt-auto-raise" );
    if ( autoRaise == MainCtx::RAISE_NEVER )
        return;
    if( THEMIM->hasVideoOutput() == true )
    {
        if( ( autoRaise & MainCtx::RAISE_VIDEO ) == 0 )
            return;
    }
    else if ( ( autoRaise & MainCtx::RAISE_AUDIO ) == 0 )
        return;
    emit askRaise();
}

#ifdef KeyPress
#undef KeyPress
#endif
void MainCtx::sendHotkey(Qt::Key key , Qt::KeyboardModifiers modifiers)
{
    QKeyEvent event(QEvent::KeyPress, key, modifiers );
    int vlckey = qtEventToVLCKey(&event);
    var_SetInteger(vlc_object_instance(p_intf), "key-pressed", vlckey);
}

void MainCtx::updateIntfScaleFactor()
{
    m_intfScaleFactor = m_intfUserScaleFactor;
    if (QWindow* window = p_intf->p_compositor ? p_intf->p_compositor->interfaceMainWindow() : nullptr)
    {
        QScreen* screen = window->screen();
        if (screen)
        {
            qreal dpi = screen->logicalDotsPerInch();
            m_intfScaleFactor = m_intfUserScaleFactor * dpi / VLC_REFERENCE_SCALE_FACTOR;
        }
    }
    emit intfScaleFactorChanged();
}

void MainCtx::onWindowVisibilityChanged(QWindow::Visibility visibility)
{
    m_windowVisibility = visibility;
}

void MainCtx::setHasAcrylicSurface(const bool v)
{
    if (m_hasAcrylicSurface == v)
        return;

    m_hasAcrylicSurface = v;
    emit hasAcrylicSurfaceChanged();
}

void MainCtx::incrementIntfUserScaleFactor(bool increment)
{
    if (increment)
        setIntfUserScaleFactor(m_intfUserScaleFactor + 0.1);
    else
        setIntfUserScaleFactor(m_intfUserScaleFactor - 0.1);
}

void MainCtx::setIntfUserScaleFactor(double newValue)
{
    m_intfUserScaleFactor = qBound(getMinIntfUserScaleFactor(), newValue, getMaxIntfUserScaleFactor());
    updateIntfScaleFactor();
}

void MainCtx::setPinVideoControls(bool pinVideoControls)
{
    if (m_pinVideoControls == pinVideoControls)
        return;

    m_pinVideoControls = pinVideoControls;
    emit pinVideoControlsChanged(m_pinVideoControls);
}

inline void MainCtx::initSystray()
{
    bool b_systrayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
    bool b_systrayWanted = var_InheritBool( p_intf, "qt-system-tray" );

    if( var_InheritBool( p_intf, "qt-start-minimized") )
    {
        if( b_systrayAvailable )
        {
            b_systrayWanted = true;
            b_hideAfterCreation = true;
        }
        else
            msg_Err( p_intf, "cannot start minimized without system tray bar" );
    }

    if( b_systrayAvailable && b_systrayWanted )
        createSystray();
}


void MainCtx::setPlaylistDocked( bool docked )
{
    b_playlistDocked = docked;

    emit playlistDockedChanged(docked);
}

void MainCtx::setPlaylistVisible( bool visible )
{
    playlistVisible = visible;

    emit playlistVisibleChanged(visible);
}

void MainCtx::setPlaylistWidthFactor( double factor )
{
    if (factor > 0.0)
    {
        playlistWidthFactor = factor;
        emit playlistWidthFactorChanged(factor);
    }
}

void MainCtx::setShowRemainingTime( bool show )
{
    m_showRemainingTime = show;
    emit showRemainingTimeChanged(show);
}

void MainCtx::setGridView(bool asGrid)
{
    m_gridView = asGrid;
    emit gridViewChanged( asGrid );
}

void MainCtx::setGrouping(Grouping grouping)
{
    m_grouping = grouping;

    emit groupingChanged(grouping);
}

void MainCtx::setInterfaceAlwaysOnTop( bool on_top )
{
    b_interfaceOnTop = on_top;
    emit interfaceAlwaysOnTopChanged(on_top);
}

bool MainCtx::hasEmbededVideo() const
{
    return m_videoSurfaceProvider && m_videoSurfaceProvider->hasVideoEmbed();
}

void MainCtx::setVideoSurfaceProvider(VideoSurfaceProvider* videoSurfaceProvider)
{
    if (m_videoSurfaceProvider)
        disconnect(m_videoSurfaceProvider, &VideoSurfaceProvider::hasVideoEmbedChanged, this, &MainCtx::hasEmbededVideoChanged);
    m_videoSurfaceProvider = videoSurfaceProvider;
    if (m_videoSurfaceProvider)
        connect(m_videoSurfaceProvider, &VideoSurfaceProvider::hasVideoEmbedChanged,
                this, &MainCtx::hasEmbededVideoChanged,
                Qt::QueuedConnection);
    emit hasEmbededVideoChanged(m_videoSurfaceProvider && m_videoSurfaceProvider->hasVideoEmbed());
}

VideoSurfaceProvider* MainCtx::getVideoSurfaceProvider() const
{
    return m_videoSurfaceProvider;
}

/*****************************************************************************
 * Systray Icon and Systray Menu
 *****************************************************************************/
/**
 * Create a SystemTray icon and a menu that would go with it.
 * Connects to a click handler on the icon.
 **/
void MainCtx::createSystray()
{
    QIcon iconVLC;
    if( QDate::currentDate().dayOfYear() >= QT_XMAS_JOKE_DAY && var_InheritBool( p_intf, "qt-icon-change" ) )
        iconVLC = QIcon::fromTheme( "vlc-xmas", QIcon( ":/logo/vlc128-xmas.png" ) );
    else
        iconVLC = QIcon::fromTheme( "vlc", QIcon( ":/logo/vlc256.png" ) );
    sysTray = new QSystemTrayIcon( iconVLC, this );
    sysTray->setToolTip( qtr( "VLC media player" ));

    systrayMenu.reset(new QMenu( qtr( "VLC media player") ));
    systrayMenu->setIcon( iconVLC );

    VLCMenuBar::updateSystrayMenu( this, p_intf, true );
    sysTray->show();

    connect( sysTray, &QSystemTrayIcon::activated,
             this, &MainCtx::handleSystrayClick );

    /* Connects on nameChanged() */
    connect( THEMIM, &PlayerController::nameChanged,
             this, &MainCtx::updateSystrayTooltipName );
    /* Connect PLAY_STATUS on the systray */
    connect( THEMIM, &PlayerController::playingStateChanged,
             this, &MainCtx::updateSystrayTooltipStatus );
}

/**
 * Updates the Systray Icon's menu and toggle the main interface
 */
void MainCtx::toggleUpdateSystrayMenu()
{
    emit toggleWindowVisibility();
    if( sysTray )
        VLCMenuBar::updateSystrayMenu( this, p_intf );
}

/* First Item of the systray menu */
void MainCtx::showUpdateSystrayMenu()
{
    emit setInterfaceVisibible(true);
    VLCMenuBar::updateSystrayMenu( this, p_intf );
}

/* First Item of the systray menu */
void MainCtx::hideUpdateSystrayMenu()
{
    emit setInterfaceVisibible(false);
    VLCMenuBar::updateSystrayMenu( this, p_intf );
}

/* Click on systray Icon */
void MainCtx::handleSystrayClick(
                                    QSystemTrayIcon::ActivationReason reason )
{
    switch( reason )
    {
        case QSystemTrayIcon::Trigger:
        case QSystemTrayIcon::DoubleClick:
#ifdef Q_OS_MAC
            VLCMenuBar::updateSystrayMenu( this, p_intf );
#else
            toggleUpdateSystrayMenu();
#endif
            break;
        case QSystemTrayIcon::MiddleClick:
            sysTray->showMessage( qtr( "VLC media player" ),
                    qtr( "Control menu for the player" ),
                    QSystemTrayIcon::Information, 3000 );
            break;
        default:
            break;
    }
}

/**
 * Updates the name of the systray Icon tooltip.
 * Doesn't check if the systray exists, check before you call it.
 **/
void MainCtx::updateSystrayTooltipName( const QString& name )
{
    if( name.isEmpty() )
    {
        sysTray->setToolTip( qtr( "VLC media player" ) );
    }
    else
    {
        sysTray->setToolTip( name );
        if( ( i_notificationSetting == NOTIFICATION_ALWAYS ) ||
            ( i_notificationSetting == NOTIFICATION_MINIMIZED && (m_windowVisibility == QWindow::Hidden || m_windowVisibility == QWindow::Minimized)))
        {
            sysTray->showMessage( qtr( "VLC media player" ), name,
                    QSystemTrayIcon::NoIcon, 3000 );
        }
    }

    VLCMenuBar::updateSystrayMenu( this, p_intf );
}

/**
 * Updates the status of the systray Icon tooltip.
 * Doesn't check if the systray exists, check before you call it.
 **/
void MainCtx::updateSystrayTooltipStatus( PlayerController::PlayingState )
{
    VLCMenuBar::updateSystrayMenu( this, p_intf );
}


/************************************************************************
 * D&D Events
 ************************************************************************/

/**
 * dropEventPlay
 *
 * Event called if something is dropped onto a VLC window
 * \param event the event in question
 * \param b_play whether to play the file immediately
 * \return nothing
 */
void MainCtx::dropEventPlay( QDropEvent *event, bool b_play )
{
    if( event->possibleActions() & ( Qt::CopyAction | Qt::MoveAction | Qt::LinkAction ) )
       event->setDropAction( Qt::CopyAction );
    else
        return;

    const QMimeData *mimeData = event->mimeData();

    /* D&D of a subtitles file, add it on the fly */
    if( mimeData->urls().count() == 1 && THEMIM->hasInput() )
    {
        if( !THEMIM->AddAssociatedMedia(SPU_ES, mimeData->urls()[0].toString(), true, true, true) )
        {
            event->accept();
            return;
        }
    }

    QVector<vlc::playlist::Media> medias;
    for( const QUrl &url: mimeData->urls() )
    {
        if( url.isValid() )
        {
            QString mrl = toURI( url.toEncoded().constData() );
#ifdef _WIN32
            QFileInfo info( url.toLocalFile() );
            if( info.exists() && info.isSymLink() )
            {
                QString target = info.symLinkTarget();
                QUrl url;
                if( QFile::exists( target ) )
                {
                    url = QUrl::fromLocalFile( target );
                }
                else
                {
                    url.setUrl( target );
                }
                mrl = toURI( url.toEncoded().constData() );
            }
#endif
            if( mrl.length() > 0 )
                medias.push_back( vlc::playlist::Media{ mrl, QString {} });
        }
    }

    /* Browsers give content as text if you dnd the addressbar,
       so check if mimedata has valid url in text and use it
       if we didn't get any normal Urls()*/
    if( !mimeData->hasUrls() && mimeData->hasText() &&
        QUrl(mimeData->text()).isValid() )
    {
        QString mrl = toURI( mimeData->text() );
        medias.push_back( vlc::playlist::Media{ mrl, QString {} });
    }
    if (!medias.empty())
        THEMPL->append(medias, b_play);
    event->accept();
}

/************************************************************************
 * Events stuff
 ************************************************************************/

bool MainCtx::onWindowClose( QWindow* )
{
    PlaylistControllerModel* playlistController = p_intf->p_mainPlaylistController;
    PlayerController* playerController = p_intf->p_mainPlayerController;

    if (m_videoSurfaceProvider)
        m_videoSurfaceProvider->onWindowClosed();
    //We need to make sure that noting is playing anymore otherwise the vout will be closed
    //after the main interface, and it requires (at least with OpenGL) that the OpenGL context
    //from the main window is still valid.
    //vout_window_ReportClose is currently stubbed
    if (playerController && playerController->hasVideoOutput()) {
        connect(playerController, &PlayerController::playingStateChanged, [this](PlayerController::PlayingState state){
            if (state == PlayerController::PLAYING_STATE_STOPPED) {
                emit askToQuit();
            }
        });
        playlistController->stop();
        return false;
    }
    else
    {
        emit askToQuit(); /* ask THEDP to quit, so we have a unique method */
        return true;
    }
}

void MainCtx::toggleInterfaceFullScreen()
{
    emit setInterfaceFullScreen( m_windowVisibility != QWindow::FullScreen );
}

void MainCtx::emitBoss()
{
    emit askBoss();
}

void MainCtx::emitShow()
{
    emit askShow();
}

void MainCtx::emitRaise()
{
    emit askRaise();
}

VLCVarChoiceModel* MainCtx::getExtraInterfaces()
{
    return m_extraInterfaces;
}

/*****************************************************************************
 * PopupMenuCB: callback triggered by the intf-popupmenu playlist variable.
 *  We don't show the menu directly here because we don't want the
 *  caller to block for a too long time.
 *****************************************************************************/
static int PopupMenuCB( vlc_object_t *, const char *,
                        vlc_value_t, vlc_value_t new_val, void *param )
{
    qt_intf_t *p_intf = (qt_intf_t *)param;

    if( p_intf->pf_show_dialog )
    {
        p_intf->pf_show_dialog( p_intf->intf, INTF_DIALOG_POPUPMENU,
                                new_val.b_bool, NULL );
    }

    return VLC_SUCCESS;
}

/*****************************************************************************
 * IntfShowCB: callback triggered by the intf-toggle-fscontrol libvlc variable.
 *****************************************************************************/
static int IntfShowCB( vlc_object_t *, const char *,
                       vlc_value_t, vlc_value_t, void *param )
{
    qt_intf_t *p_intf = (qt_intf_t *)param;
    p_intf->p_mi->emitShow();

    return VLC_SUCCESS;
}

/*****************************************************************************
 * IntfRaiseMainCB: callback triggered by the intf-show-main libvlc variable.
 *****************************************************************************/
static int IntfRaiseMainCB( vlc_object_t *, const char *,
                            vlc_value_t, vlc_value_t, void *param )
{
    qt_intf_t *p_intf = (qt_intf_t *)param;
    p_intf->p_mi->emitRaise();

    return VLC_SUCCESS;
}

/*****************************************************************************
 * IntfBossCB: callback triggered by the intf-boss libvlc variable.
 *****************************************************************************/
static int IntfBossCB( vlc_object_t *, const char *,
                       vlc_value_t, vlc_value_t, void *param )
{
    qt_intf_t *p_intf = (qt_intf_t *)param;
    p_intf->p_mi->emitBoss();

    return VLC_SUCCESS;
}

bool MainCtx::acrylicActive() const
{
    return m_acrylicActive;
}

void MainCtx::setAcrylicActive(bool newAcrylicActive)
{
    if (m_acrylicActive == newAcrylicActive)
        return;

    m_acrylicActive = newAcrylicActive;
    emit acrylicActiveChanged();
}

bool MainCtx::preferHotkeys() const
{
    return m_preferHotkeys;
}

void MainCtx::setPreferHotkeys(bool enable)
{
    if (m_preferHotkeys == enable)
        return;

    m_preferHotkeys = enable;

    emit preferHotkeysChanged();
}

QWindow *MainCtx::intfMainWindow() const
{
    if (p_intf->p_compositor)
        return p_intf->p_compositor->interfaceMainWindow();
    else
        return nullptr;
}

QVariant MainCtx::settingValue(const QString &key, const QVariant &defaultValue) const
{
    return settings->value(key, defaultValue);
}

void MainCtx::setSettingValue(const QString &key, const QVariant &value)
{
    settings->setValue(key, value);
}
