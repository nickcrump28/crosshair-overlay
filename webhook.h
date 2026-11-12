/* Where bug reports go.
 *
 * The app no longer contains your Discord webhook. Reports are sent to a small relay you run for free
 * on Cloudflare (report-relay-worker.js, steps in README.txt). The relay holds the real webhook as a
 * secret, only accepts proper reports, limits how often one PC can send, and can only ping you.
 * Put the relay's address between the quotes (e.g. L"https://crosshair-reports.yourname.workers.dev")
 * and rebuild. Leave it "" to turn sending off: the app still keeps its log, and "Report a problem"
 * saves a report file instead.
 */
#ifndef WEBHOOK_H
#define WEBHOOK_H
/* Discord user to ping on every report (the relay enforces this too). "" = no ping. */
#define BUG_PING_USER_ID "329379772826124288"
/* Discord role the publisher can ping when you publish a release. "" = no ping. */
#define RELEASE_PING_ROLE_ID "1569501464659894312"
#ifndef REPORT_RELAY_URL
#define REPORT_RELAY_URL L""
#endif
/* Without a relay, reports go straight to this Discord webhook (it's inside the exe, so anyone could
   find it and post to that one channel; if it gets spammed, delete it in Discord, make a new one,
   paste it here and rebuild). Used only while REPORT_RELAY_URL is "". */
#ifndef BUG_WEBHOOK_URL
#define BUG_WEBHOOK_URL L""
#endif
/* Optional: a separate webhook for the opt-in usage stats (one short line per player per week).
   Leave "" to post them to the bug-report webhook. */
#ifndef STATS_WEBHOOK_URL
#define STATS_WEBHOOK_URL L""
#endif
#endif
