# discord-rpc (vendor)

Official [discord/discord-rpc](https://github.com/discord/discord-rpc) — IPC Rich Presence for desktop apps.

Used by AntiZapret for Discord activity ("Playing AntiZapret").

**Setup:**
1. Create an application at https://discord.com/developers/applications
2. Copy **Application ID** (Client ID)
3. Upload Rich Presence art assets in the Developer Portal (Large/Small Image keys)
4. Wire Discord_Initialize / Discord_UpdatePresence in the app with that Client ID

Note: upstream marks this library as deprecated in favor of Discord Social/Game SDK; for local Rich Presence without OAuth it remains the simplest integration for a Win32 desktop client.
