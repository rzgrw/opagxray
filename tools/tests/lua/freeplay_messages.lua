-- Loads the CoC freeplay adapter into a bare environment and prints its player messages, one per line,
-- as `<case>\t<text>`. usage: luajit freeplay_messages.lua <path to xrsim_freeplay.script>
local env = setmetatable({}, {__index = _G})
local chunk = assert(loadfile(assert(arg[1], "script path")))
setfenv(chunk, env)
chunk()

local function out(case, text)
	io.write(case, "\t", tostring(text), "\n")
end

out("live", env.activation_message(
	"mode=live provider=claude_cli model=sonnet max_in_flight=1 calls_per_minute=6 reconsider_ms=600000 min_wake_ms=45000"))
out("live_api", env.activation_message(
	"mode=live provider=anthropic model=claude-x max_in_flight=2 calls_per_minute=0 reconsider_ms=600000 min_wake_ms=45000"))
out("recorded", env.activation_message("mode=recorded"))
for _, reason in ipairs({"cli_unavailable", "missing_api_key", "missing_model",
	"invalid_setting:XRAY_FREEPLAY_MAX_IN_FLIGHT", "provider_disabled", "unsupported_platform"}) do
	out("refusal:" .. reason, env.refusal_message(reason))
end
for _, reason in ipairs({"cli_exit", "cli_timeout", "queue_shutdown"}) do
	out("failure:" .. reason, env.failure_message(reason))
end
local shown = {}
local base = "mode=live provider=claude_cli model=sonnet max_in_flight=1 calls_per_minute=6"
out("notice:none", env.failure_notice(base, shown))
out("notice:first", env.failure_notice(base .. " last_failure=cli_exit", shown))
out("notice:repeat", env.failure_notice(base .. " last_failure=cli_exit", shown))
out("notice:other", env.failure_notice(base .. " last_failure=cli_timeout", shown))
