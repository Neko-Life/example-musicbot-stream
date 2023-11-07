/*
    Minimal example to stream youtube music using yt-dlp and ffmpeg with popen
*/

#include <dpp/dpp.h>

#define YTDLP_CMD "../Musicat/libs/yt-dlp/yt-dlp.sh"
#define FFMPEG_CMD "ffmpeg"

// fill this with your own bot token
#define BOT_TOKEN ""

// cache for disconnecting from voice channel
// as most voice event `from` field can be null
std::map<dpp::snowflake, dpp::discord_client *> discord_clients;
// always use mutex to protect global resource in multi-threaded application
std::mutex discord_clients_m;

void handle_ready(const dpp::ready_t &event) {
  fprintf(stderr, "Bot ready!\n");

  // register command on ready
  if (dpp::run_once<struct register_bot_commands>()) {
    const dpp::slashcommand slash_stream(
        "stream", "Join and immediately stream music from youtube",
        event.from->creator->me.id);

    event.from->creator->global_command_create(slash_stream);
  }
}

void handle_slashcommand(const dpp::slashcommand_t &event) {
  const std::string cmd = event.command.get_command_name();

  if (cmd == "stream") {
    // find the guild to connect to
    dpp::guild *g = dpp::find_guild(event.command.guild_id);

    if (!g) {
      event.reply("GUILD NOT FOUND!! WHAT IS THIS SORCERY??");
      return;
    }

    if (!g->connect_member_voice(event.command.usr.id)) {
      // you should be in voice channel when invoking this command
      event.reply("Please join a voice channel first");
      return;
    }

    {
      std::lock_guard lk(discord_clients_m);
      // save discord client to use for disconnecting on marker end
      discord_clients.insert_or_assign(event.command.guild_id, event.from);
    }

    event.reply("Joining...");
  }
}

void handle_streaming(const dpp::voice_ready_t event) {
  FILE *read_stream = popen(
      YTDLP_CMD " -f 251 -o - https://www.youtube.com/watch?v=mqoEplBvXkI "
                "| " FFMPEG_CMD " -acodec opus -i - -f s16le -ar 48000 -ac 2 -",
      "r");

  // should always be this size when using send_audio_raw to avoid
  // distortion
  constexpr size_t bufsize = dpp::send_audio_raw_max_length;

  char buf[bufsize];
  ssize_t buf_read = 0;
  ssize_t current_read = 0;

  while ((current_read = fread(buf, 1, bufsize - buf_read, read_stream)) > 0) {
    buf_read += current_read;

    // queue buffer only when it's exactly `bufsize` size
    if (buf_read == bufsize) {
      event.voice_client->send_audio_raw((uint16_t *)buf, buf_read);
      buf_read = 0;
    }
  }

  // queue the last buffer if any, usually size less than `bufsize`
  if (buf_read > 0) {
    event.voice_client->send_audio_raw((uint16_t *)buf, buf_read);
    buf_read = 0;
  }

  // done processing all stream buffer, clean up
  // "done" here means done queueing audio buffer
  // not done streaming the queued audio data to discord
  pclose(read_stream);
  read_stream = NULL;

  // wait until voice client done streaming all queued buffer
  while (event.voice_client->is_playing()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // insert marker to disconnect
  // this will fire a voice_track_marker event
  // you can also insert any metadata you need for
  // other purpose eg. play next song in your playlist
  event.voice_client->insert_marker("disconnect");
}

void handle_voice_ready(const dpp::voice_ready_t &event) {
  // start streaming to whichever voice channel this event came from
  // this spawns and do the streaming in another thread so the event thread
  // won't get blocked
  std::thread t(handle_streaming, event);

  t.detach();
}

void handle_voice_track_marker(const dpp::voice_track_marker_t &event) {
  if (event.track_meta == "disconnect") {
    // you need to do this in another thread to avoid deadlock since the
    // voice_track_marker event itself is currently locking some mutex when
    // calling this very handler
    std::thread t([event]() {
      std::lock_guard lk(discord_clients_m);

      auto i = discord_clients.find(event.voice_client->server_id);

      if (i == discord_clients.end())
        return;

      i->second->disconnect_voice(event.voice_client->server_id);

      // don't need it anymore, erase it
      discord_clients.erase(i);
    });

    t.detach();
  }
}

int main() {
  dpp::cluster client(BOT_TOKEN, dpp::i_default_intents);

  // register all needed event handler
  client.on_ready(handle_ready);
  client.on_slashcommand(handle_slashcommand);
  client.on_voice_ready(handle_voice_ready);
  client.on_voice_track_marker(handle_voice_track_marker);

  // start the bot and block main thread
  client.start(false);

  return 0;
}
