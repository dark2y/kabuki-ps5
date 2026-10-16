// What the panel shows and what its buttons ask for: librespot-ps5's player, as the panel sees it.
//
// On the console this mirrors librespot-ps5 (its control.rs channel); the PC preview fills it
// with sample data.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace overlay
{

enum class Repeat
{
    off,
    all,
    one,
};

// A track to come, as librespot-ps5's queue lists it.
struct QueueItem
{
    std::string title;
    std::string artist;
    std::string cover; // a JPEG or PNG file; empty for none
    std::string uri; // Spotify URI used to play this entry
    int index = -1; // position in librespot's full queue, before metadata filtering
};

struct PlayerState
{
    std::string title;
    std::string artist;
    std::string album;
    std::string cover; // a JPEG or PNG file; empty for the console's fallback art
    int duration_ms = 0;
    int position_ms = 0; // what is heard
    bool playing = false; // what librespot is doing (the play/pause button)
    // Something asked for (a play, a seek, a new track) is not heard yet: the bar shows a
    // buffering light. librespot-ps5 holds the bar where it is heard until then.
    bool pending = false;
    bool can_previous = true;
    bool can_next = true;
    bool shuffle = false;
    Repeat repeat = Repeat::off;
    bool liked = false;
    int volume = 100; // 0 to 100
    std::string device; // the Spotify Connect name this console shows as
    // What plays next. `queue_known` is false while librespot-ps5 does not report its queue.
    std::vector<QueueItem> queue;
    bool queue_known = false;
};

// One of the user's saved playlists or albums (or Liked Songs).
struct LibraryItem
{
    std::string kind; // "playlist", "album" or "collection"
    std::string uri;
    std::string name;
    std::string subtitle;
    std::string cover; // a JPEG or PNG file; empty for none (yet)
};

// A track of an open playlist or album.
struct TrackItem
{
    std::string uri;
    std::string title;
    std::string artist;
};

// What the Queue and Library cards list, and where their selection is. The console fills
// this; the cards only draw it.
struct Browse
{
    bool focused = false; // the list holds the focus (shows its selection)
    int queue_selected = 0;
    int library_level = 0; // 0 the library, 1 an open playlist's or album's tracks
    int library_selected = 0;
    int track_selected = 0;
    std::vector<LibraryItem> library;
    bool library_known = false;
    std::string library_error;
    std::string list_uri; // the open playlist or album
    std::vector<TrackItem> tracks;
    bool tracks_known = false;
    std::string tracks_error;
    int starting_row = -1; // a row whose song is starting: it shows the buffering light
};

enum class Command
{
    play_pause,
    previous,
    next,
    shuffle,
    repeat,
    like,
    volume,
    seek,
};

struct Player
{
    PlayerState state;
    Browse browse;
    // Asked of librespot-ps5; the state changes when it answers. `value` is the volume (0 to
    // 100) for Command::volume and the position in milliseconds for Command::seek.
    std::function<void(Command, int value)> send;
};

} // namespace overlay
