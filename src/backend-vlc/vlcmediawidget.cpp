/*
 * vlcmediawidget.cpp
 *
 * Copyright (C) 2010-2011 Christoph Pfister <christophpfister@gmail.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "vlcmediawidget.h"

#include <QMouseEvent>
#include <vlc/vlc.h>
#include "../log.h"

VlcMediaWidget::VlcMediaWidget(QWidget *parent) : AbstractMediaWidget(parent), vlcInstance(NULL),
	vlcMediaPlayer(NULL), playingDvd(false)
{
}

bool VlcMediaWidget::init()
{
	const char *arguments[] = { "--no-video-title-show" };
	vlcInstance = libvlc_new(sizeof(arguments) / sizeof(arguments[0]), arguments);

	if (vlcInstance == NULL) {
		Log("VlcMediaWidget::init: cannot create vlc instance") << libvlc_errmsg();
		return false;
	}

	vlcMediaPlayer = libvlc_media_player_new(vlcInstance);

	if (vlcMediaPlayer == NULL) {
		Log("VlcMediaWidget::init: cannot create vlc media player") << libvlc_errmsg();
		return false;
	}

	libvlc_event_manager_t *eventManager = libvlc_media_player_event_manager(vlcMediaPlayer);
	libvlc_event_e eventTypes[] = { libvlc_MediaPlayerEncounteredError,
		libvlc_MediaPlayerEndReached, libvlc_MediaPlayerLengthChanged,
		libvlc_MediaPlayerSeekableChanged, libvlc_MediaPlayerStopped,
		libvlc_MediaPlayerTimeChanged };

	for (uint i = 0; i < (sizeof(eventTypes) / sizeof(eventTypes[0])); ++i) {
		if (libvlc_event_attach(eventManager, eventTypes[i], eventHandler, this) != 0) {
			Log("VlcMediaWidget::init: cannot attach event handler") << eventTypes[i];
			return false;
		}
	}

	libvlc_media_player_set_xwindow(vlcMediaPlayer, winId());
	setAttribute(Qt::WA_NativeWindow);
	setAttribute(Qt::WA_PaintOnScreen);
	return true;
}

VlcMediaWidget::~VlcMediaWidget()
{
	if (vlcMediaPlayer != NULL) {
		libvlc_media_player_release(vlcMediaPlayer);
	}

	if (vlcInstance != NULL) {
		libvlc_release(vlcInstance);
	}
}

VlcMediaWidget *VlcMediaWidget::createVlcMediaWidget(QWidget *parent)
{
	QScopedPointer<VlcMediaWidget> vlcMediaWidget(new VlcMediaWidget(parent));

	if (!vlcMediaWidget->init()) {
		return NULL;
	}

	return vlcMediaWidget.take();
}

MediaWidget::PlaybackStatus VlcMediaWidget::getPlaybackStatus()
{
	MediaWidget::PlaybackStatus playbackStatus = MediaWidget::Playing;

	switch (libvlc_media_player_get_state(vlcMediaPlayer)) {
	case libvlc_NothingSpecial:
	case libvlc_Stopped:
		playbackStatus = MediaWidget::Idle;
		break;
	case libvlc_Opening:
	case libvlc_Buffering:
	case libvlc_Playing:
		playbackStatus = MediaWidget::Playing;
		break;
	case libvlc_Paused:
		playbackStatus = MediaWidget::Paused;
		break;
	case libvlc_Ended:
	case libvlc_Error:
		playbackStatus = MediaWidget::Idle;
		// don't keep last picture shown
		libvlc_media_player_stop(vlcMediaPlayer);
		break;
	}

	if (playbackStatus == MediaWidget::Idle) {
		playingDvd = false;
	}

	return playbackStatus;
}

int VlcMediaWidget::getTotalTime()
{
	int totalTime = libvlc_media_player_get_length(vlcMediaPlayer);

	if (totalTime < 0) {
		totalTime = 0;
	}

	return totalTime;
}

int VlcMediaWidget::getCurrentTime()
{
	int currentTime = libvlc_media_player_get_time(vlcMediaPlayer);

	if (currentTime < 0) {
		currentTime = 0;
	}

	return currentTime;
}

bool VlcMediaWidget::isSeekable()
{
	return libvlc_media_player_is_seekable(vlcMediaPlayer);
}

QMap<MediaWidget::MetadataType, QString> VlcMediaWidget::getMetadata()
{
	QMap<MediaWidget::MetadataType, QString> metadata;
	libvlc_media_t *media = libvlc_media_player_get_media(vlcMediaPlayer);

	if (media != NULL) {
		metadata.insert(MediaWidget::Title,
			QString::fromUtf8(libvlc_media_get_meta(media, libvlc_meta_Title)));
		metadata.insert(MediaWidget::Artist,
			QString::fromUtf8(libvlc_media_get_meta(media, libvlc_meta_Artist)));
		metadata.insert(MediaWidget::Album,
			QString::fromUtf8(libvlc_media_get_meta(media, libvlc_meta_Album)));
		metadata.insert(MediaWidget::TrackNumber,
			QString::fromUtf8(libvlc_media_get_meta(media, libvlc_meta_TrackNumber)));
	}

	return metadata;
}

QStringList VlcMediaWidget::getAudioStreams()
{
	QStringList audioStreams;
	libvlc_track_description_t *track = libvlc_audio_get_track_description(vlcMediaPlayer);

	if (track != NULL) {
		// skip the 'deactivate' audio channel
		track = track->p_next;
	}

	while (track != NULL) {
		QString audioStream = QString::fromUtf8(track->psz_name);
		int cutBegin = (audioStream.indexOf('[') + 1);

		if (cutBegin > 0) {
			int cutEnd = audioStream.lastIndexOf(']');

			if (cutEnd >= 0) {
				// remove unnecessary text
				audioStream = audioStream.mid(cutBegin, cutEnd - cutBegin);
			}
		}

		if (audioStream.isEmpty()) {
			audioStream = QString::number(audioStreams.size() + 1);
		}

		audioStreams.append(audioStream);
		track = track->p_next;
	}

	return audioStreams;
}

int VlcMediaWidget::getCurrentAudioStream()
{
	// skip the 'deactivate' audio channel
	return (libvlc_audio_get_track(vlcMediaPlayer) - 1);
}

QStringList VlcMediaWidget::getSubtitles()
{
	QStringList subtitles;
	libvlc_track_description_t *track = libvlc_video_get_spu_description(vlcMediaPlayer);

	if (track != NULL) {
		// skip the 'deactivate' subtitle
		track = track->p_next;
	}

	while (track != NULL) {
		QString subtitle = QString::fromUtf8(track->psz_name);
		int cutBegin = (subtitle.indexOf('[') + 1);

		if (cutBegin > 0) {
			int cutEnd = subtitle.lastIndexOf(']');

			if (cutEnd >= 0) {
				// remove unnecessary text
				subtitle = subtitle.mid(cutBegin, cutEnd - cutBegin);
			}
		}

		if (subtitle.isEmpty()) {
			subtitle = QString::number(subtitles.size() + 1);
		}

		subtitles.append(subtitle);
		track = track->p_next;
	}

	return subtitles;
}

int VlcMediaWidget::getCurrentSubtitle()
{
	// skip the 'deactivate' subtitle
	return (libvlc_video_get_spu(vlcMediaPlayer) - 1);
}

int VlcMediaWidget::getTitleCount()
{
	return libvlc_media_player_get_title_count(vlcMediaPlayer);
}

int VlcMediaWidget::getCurrentTitle()
{
	return libvlc_media_player_get_title(vlcMediaPlayer);
}

int VlcMediaWidget::getChapterCount()
{
	return libvlc_media_player_get_chapter_count(vlcMediaPlayer);
}

int VlcMediaWidget::getCurrentChapter()
{
	return libvlc_media_player_get_chapter(vlcMediaPlayer);
}

int VlcMediaWidget::getAngleCount()
{
	// FIXME
	return 0;
}

int VlcMediaWidget::getCurrentAngle()
{
	// FIXME
	return -1;
}

bool VlcMediaWidget::hasMenu()
{
	return playingDvd;
}

QSize VlcMediaWidget::getVideoSize()
{
	// FIXME
	return QSize();
}

void VlcMediaWidget::setMuted(bool muted)
{
	libvlc_audio_set_mute(vlcMediaPlayer, muted);
}

void VlcMediaWidget::setVolume(int volume)
{
	// 0 <= volume <= 200
	if (libvlc_audio_set_volume(vlcMediaPlayer, volume) != 0) {
		Log("VlcMediaWidget::setVolume: cannot set volume") << volume;
	}
}

void VlcMediaWidget::setAspectRatio(MediaWidget::AspectRatio aspectRatio)
{
	// "", "1:1", "4:3", "5:4", 16:9", "16:10", "221:100", "235:100", "239:100"
	const char *vlcAspectRatio = "";
	int vlcScaleFactor = 1;

	switch (aspectRatio) {
	case MediaWidget::AspectRatioAuto:
		break;
	case MediaWidget::AspectRatio4_3:
		vlcAspectRatio = "4:3";
		break;
	case MediaWidget::AspectRatio16_9:
		vlcAspectRatio = "16:9";
		break;
	case MediaWidget::AspectRatioWidget:
		// zero = adjust video to window
		vlcScaleFactor = 0;
		break;
	}

	libvlc_video_set_aspect_ratio(vlcMediaPlayer, vlcAspectRatio);
	libvlc_video_set_scale(vlcMediaPlayer, vlcScaleFactor);
}

void VlcMediaWidget::setDeinterlacing(bool deinterlacing)
{
	// "", "blend", "bob", "discard", "ivtc", "linear",
	// "mean", "phosphor", "x", "yadif", "yadif2x"
	const char *vlcDeinterlaceMode = "";

	if (deinterlacing) {
		vlcDeinterlaceMode = "yadif";
	}

	libvlc_video_set_deinterlace(vlcMediaPlayer, vlcDeinterlaceMode);
}

void VlcMediaWidget::play(const MediaSource &source)
{
	QByteArray url = source.getUrl().toEncoded();
	playingDvd = false;

	switch (source.getType()) {
	case MediaSource::Url:
		if (url.endsWith(".iso")) {
			playingDvd = true;
		}

		break;
	case MediaSource::AudioCd:
		if (url.size() >= 7) {
			url.replace(0, 4, "cdda");
		} else {
			url = "cdda://";
		}

		break;
	case MediaSource::VideoCd:
		if (url.size() >= 7) {
			url.replace(0, 4, "vcd");
		} else {
			url = "vcd://";
		}

		break;
	case MediaSource::Dvd:
		if (url.size() >= 7) {
			url.replace(0, 4, "dvd");
		} else {
			url = "dvd://";
		}

		playingDvd = true;
		break;
	case MediaSource::Dvb:
		break;
	}

	libvlc_media_t *vlcMedia = libvlc_media_new_location(vlcInstance, url.constData());

	if (vlcMedia == NULL) {
		libvlc_media_player_stop(vlcMediaPlayer);
		Log("VlcMediaWidget::play: cannot create media") << source.getUrl().prettyUrl();
		return;
	}

	libvlc_event_manager_t *eventManager = libvlc_media_event_manager(vlcMedia);
	libvlc_event_e eventTypes[] = { libvlc_MediaMetaChanged };

	for (uint i = 0; i < (sizeof(eventTypes) / sizeof(eventTypes[0])); ++i) {
		if (libvlc_event_attach(eventManager, eventTypes[i], eventHandler, this) != 0) {
			Log("VlcMediaWidget::play: cannot attach event handler") << eventTypes[i];
		}
	}

	libvlc_media_player_set_media(vlcMediaPlayer, vlcMedia);
	libvlc_media_release(vlcMedia);

//	FIXME!

// 	if (source.subtitleUrl.isValid()) {
// 		if (libvlc_video_set_subtitle_file(vlcMediaPlayer,
// 		    source.subtitleUrl.toEncoded().constData()) == 0) {
// 			Log("VlcMediaWidget::play: cannot set subtitle file") <<
// 				source.subtitleUrl.prettyUrl();
// 		}
// 	}

	if (libvlc_media_player_play(vlcMediaPlayer) != 0) {
		Log("VlcMediaWidget::play: cannot play media") << source.getUrl().prettyUrl();
	}
}

void VlcMediaWidget::stop()
{
	libvlc_media_player_stop(vlcMediaPlayer);
}

void VlcMediaWidget::setPaused(bool paused)
{
	libvlc_media_player_set_pause(vlcMediaPlayer, paused);
	// we don't monitor playing / buffering / paused state changes
	updatePlaybackStatus(getPlaybackStatus());
}

void VlcMediaWidget::seek(int time)
{
	libvlc_media_player_set_time(vlcMediaPlayer, time);
}

void VlcMediaWidget::setCurrentAudioStream(int currentAudioStream)
{
	// skip the 'deactivate' audio channel
	libvlc_audio_set_track(vlcMediaPlayer, currentAudioStream + 1);
}

void VlcMediaWidget::setCurrentSubtitle(int currentSubtitle)
{
	// skip the 'deactivate' subtitle
	libvlc_video_set_spu(vlcMediaPlayer, currentSubtitle + 1);
}

void VlcMediaWidget::setExternalSubtitle(const KUrl &subtitleUrl)
{
	if (libvlc_video_set_subtitle_file(vlcMediaPlayer,
	    subtitleUrl.toEncoded().constData()) == 0) {
		Log("VlcMediaWidget::setExternalSubtitle: cannot set subtitle file") <<
			subtitleUrl.prettyUrl();
	}
}

void VlcMediaWidget::setCurrentTitle(int currentTitle)
{
	libvlc_media_player_set_title(vlcMediaPlayer, currentTitle);
}

void VlcMediaWidget::setCurrentChapter(int currentChapter)
{
	libvlc_media_player_set_chapter(vlcMediaPlayer, currentChapter);
}

void VlcMediaWidget::setCurrentAngle(int currentAngle)
{
	Q_UNUSED(currentAngle)
	// FIXME
}

bool VlcMediaWidget::jumpToPreviousChapter()
{
	int currentTitle = libvlc_media_player_get_title(vlcMediaPlayer);
	int currentChapter = libvlc_media_player_get_chapter(vlcMediaPlayer);
	libvlc_media_player_previous_chapter(vlcMediaPlayer);

	if ((libvlc_media_player_get_title(vlcMediaPlayer) != currentTitle) ||
	    (libvlc_media_player_get_chapter(vlcMediaPlayer) != currentChapter)) {
		return true;
	}

	return false;
}

bool VlcMediaWidget::jumpToNextChapter()
{
	int currentTitle = libvlc_media_player_get_title(vlcMediaPlayer);
	int currentChapter = libvlc_media_player_get_chapter(vlcMediaPlayer);
	libvlc_media_player_next_chapter(vlcMediaPlayer);

	if ((libvlc_media_player_get_title(vlcMediaPlayer) != currentTitle) ||
	    (libvlc_media_player_get_chapter(vlcMediaPlayer) != currentChapter)) {
		return true;
	}

	return false;
}

void VlcMediaWidget::showDvdMenu()
{
	// FIXME
}

void VlcMediaWidget::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		// FIXME needs vlc >= 1.2
		// libvlc_media_player_navigate(vlcMediaPlayer, libvlc_navigate_activate);
	}

	AbstractMediaWidget::mousePressEvent(event);
}

void VlcMediaWidget::eventHandler(const libvlc_event_t *event, void *instance)
{
	// FIXME recheck
	VlcMediaWidget *mediaWidget = reinterpret_cast<VlcMediaWidget *>(instance);

	switch (event->type) {
	case libvlc_MediaMetaChanged:
		mediaWidget->resetBaseState();
		return;
	case libvlc_MediaPlayerEncounteredError:
		mediaWidget->updatePlaybackStatus(mediaWidget->getPlaybackStatus());
		return;
	case libvlc_MediaPlayerEndReached:
		mediaWidget->playbackFinished();
		mediaWidget->updatePlaybackStatus(mediaWidget->getPlaybackStatus());
		return;
	case libvlc_MediaPlayerLengthChanged:
		mediaWidget->updateCurrentTotalTime(mediaWidget->getCurrentTime(), mediaWidget->getTotalTime());
		return;
	case libvlc_MediaPlayerSeekableChanged:
		mediaWidget->updateSeekable(mediaWidget->isSeekable());
		return;
	case libvlc_MediaPlayerStopped:
		mediaWidget->updatePlaybackStatus(mediaWidget->getPlaybackStatus());
		return;
	case libvlc_MediaPlayerTimeChanged:
		mediaWidget->updateCurrentTotalTime(mediaWidget->getCurrentTime(), mediaWidget->getTotalTime());
		return;
	}

	Log("VlcMediaWidget::eventHandler: unknown event type") << event->type;
}