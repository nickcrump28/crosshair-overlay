// Crosshair Overlay - bug report relay (Cloudflare Worker, free plan)
//
// The app sends bug reports here instead of straight to Discord, so the Discord webhook is never
// inside the app. This relay:
//   - only accepts real Crosshair Overlay reports (the message format the app sends + one .txt file),
//   - limits how often one network can send (2 per minute, 10 per hour),
//   - only ever pings you (PING_USER_ID) - "@everyone" / "@here" typed by someone does nothing,
//   - keeps the real webhook secret (DISCORD_WEBHOOK is a Cloudflare secret, not in this file).
//
// Setup (about 5 minutes) - see README.txt "BUG REPORT RELAY":
//   Cloudflare dashboard > Workers & Pages > Create > Worker > name it (e.g. crosshair-reports) > Deploy >
//   Edit code > paste this whole file > Deploy.
//   Settings > Variables and Secrets: add Secret DISCORD_WEBHOOK = your bug-report webhook URL,
//   and Text PING_USER_ID = your Discord user ID. Then put the worker's URL in webhook.h.

const MAX_BYTES = 2 * 1024 * 1024;
const PER_MINUTE = 2;
const PER_HOUR = 10;
const recent = new Map(); // network -> times it sent (kept while this copy of the worker is running)

function reply(status, text) {
  return new Response(text, { status, headers: { "content-type": "text/plain" } });
}

export default {
  async fetch(request, env) {
    if (request.method !== "POST") return reply(200, "Crosshair Overlay report relay is running.");
    if (!env.DISCORD_WEBHOOK) return reply(500, "DISCORD_WEBHOOK secret is not set");

    // how often this network has sent
    const who = request.headers.get("CF-Connecting-IP") || "unknown";
    const now = Date.now();
    const times = (recent.get(who) || []).filter((t) => now - t < 3600 * 1000);
    if (times.filter((t) => now - t < 60 * 1000).length >= PER_MINUTE || times.length >= PER_HOUR)
      return reply(429, "Too many reports - try again later.");
    if (recent.size > 5000) recent.clear();

    // it has to look exactly like a report from the app
    if (Number(request.headers.get("content-length") || 0) > MAX_BYTES) return reply(413, "Too big.");
    if (!(request.headers.get("content-type") || "").startsWith("multipart/form-data")) return reply(400, "Not a report.");
    let form;
    try {
      form = await request.formData();
    } catch {
      return reply(400, "Not a report.");
    }
    const payloadJson = form.get("payload_json");
    const file = form.get("files[0]");
    if (typeof payloadJson !== "string" || !file || typeof file === "string") return reply(400, "Not a report.");
    if (file.size > MAX_BYTES || !String(file.name || "").endsWith(".txt")) return reply(400, "Not a report.");
    let payload;
    try {
      payload = JSON.parse(payloadJson);
    } catch {
      return reply(400, "Not a report.");
    }
    let content = String(payload.content || "");
    if (!/\*\*(App crash \(automatic\)|Crash \(automatic\)|Error \(automatic\)|Update undone \(automatic\)|Problem report|Roblox crash \(automatic\))\*\*/.test(content))
      return reply(400, "Not a report.");
    content = content.slice(0, 1900).replace(/@(everyone|here)/g, "@​$1");
    const report = (await file.text()).slice(0, MAX_BYTES);
    if (!report.startsWith("Crosshair Overlay v")) return reply(400, "Not a report.");

    // forward to Discord with only your ping allowed
    const out = new FormData();
    out.append(
      "payload_json",
      JSON.stringify({
        username: "Crosshair Overlay reports",
        content,
        embeds: Array.isArray(payload.embeds) ? JSON.parse(JSON.stringify(payload.embeds.slice(0, 1)).replace(/@(everyone|here)/g, "@\u200b$1")) : undefined,
        allowed_mentions: { parse: [], users: env.PING_USER_ID ? [String(env.PING_USER_ID)] : [] },
      })
    );
    out.append("files[0]", new Blob([report], { type: "text/plain" }), "report.txt");
    const r = await fetch(env.DISCORD_WEBHOOK, { method: "POST", body: out });
    times.push(now);
    recent.set(who, times);
    return r.ok ? reply(200, "ok") : reply(502, "Discord didn't accept it (" + r.status + ").");
  },
};
