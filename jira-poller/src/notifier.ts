import { config } from "./config.ts";
import { extractDeveloper, type JiraIssue } from "./jira.ts";

export async function postTrigger(issue: JiraIssue): Promise<boolean> {
  const status = issue.fields.status?.name ?? config.jira.targetStatus;
  const developer = extractDeveloper(issue);
  const payload: Record<string, string> = {
    event: "celebrate",
    source: "jira-poller",
    message: `${issue.key} → ${status}`,
  };
  if (developer) payload.developer = developer;

  try {
    const res = await fetch(config.notifier.url, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify(payload),
    });
    if (!res.ok) {
      const body = await res.text().catch(() => "");
      console.error(`[notifier] ${res.status} for ${issue.key}: ${body.slice(0, 200)}`);
      return false;
    }
    return true;
  } catch (err) {
    console.error(`[notifier] network error for ${issue.key}:`, (err as Error).message);
    return false;
  }
}
