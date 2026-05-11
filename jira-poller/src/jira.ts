import { authHeader, config } from "./config.ts";

export type JiraUser = { accountId?: string; displayName?: string };
export type JiraIssue = {
  key: string;
  fields: {
    summary?: string;
    status?: { name?: string };
    [customField: string]: unknown;
  };
};
export type JiraField = { id: string; name: string };

export function buildJql(): string {
  const { targetStatus, updatedWindow, projectKey, assignee, jqlExtra } = config.jira;
  const clauses: string[] = [
    `status = "${targetStatus}"`,
    `updated >= "${updatedWindow}"`,
  ];
  if (projectKey) clauses.push(`project = "${projectKey}"`);

  const a = assignee.trim();
  if (a && a !== "all") {
    if (a === "me") clauses.push(`assignee = currentUser()`);
    else if (a === "any") clauses.push(`assignee is not EMPTY`);
    else clauses.push(`assignee = "${a}"`);
  }

  if (jqlExtra?.trim()) clauses.push(jqlExtra.trim());
  return clauses.join(" AND ") + " ORDER BY updated DESC";
}

export function extractDeveloper(issue: JiraIssue): string | undefined {
  const field = config.jira.developerField;
  if (!field) return undefined;
  const raw = issue.fields[field];
  if (!raw) return undefined;
  const user = raw as JiraUser;
  return user.displayName ?? user.accountId ?? undefined;
}

export async function fetchMatchingIssues(jql: string): Promise<JiraIssue[]> {
  const { baseUrl, pageSize, maxTotal, developerField } = config.jira;
  const all: JiraIssue[] = [];
  let nextPageToken: string | undefined;

  while (all.length < maxTotal) {
    const url = new URL("/rest/api/3/search/jql", baseUrl);
    url.searchParams.set("jql", jql);
    const fieldList = ["summary", "status"];
    if (developerField) fieldList.push(developerField);
    url.searchParams.set("fields", fieldList.join(","));
    url.searchParams.set("maxResults", String(pageSize));
    if (nextPageToken) url.searchParams.set("nextPageToken", nextPageToken);

    const res = await fetch(url, {
      headers: { authorization: authHeader, accept: "application/json" },
    });

    if (!res.ok) {
      const body = await res.text().catch(() => "");
      console.error(`[jira] ${res.status} ${res.statusText}: ${body.slice(0, 300)}`);
      return all;
    }

    const data = (await res.json()) as {
      issues?: JiraIssue[];
      nextPageToken?: string;
      isLast?: boolean;
    };
    all.push(...(data.issues ?? []));

    if (data.isLast || !data.nextPageToken) break;
    nextPageToken = data.nextPageToken;
  }

  if (all.length >= maxTotal) {
    console.warn(`[jira] hit JIRA_MAX_TOTAL=${maxTotal} cap; results may be truncated`);
  }
  return all;
}

export async function listAllFields(): Promise<JiraField[]> {
  const url = new URL("/rest/api/3/field", config.jira.baseUrl);
  const res = await fetch(url, {
    headers: { authorization: authHeader, accept: "application/json" },
  });
  if (!res.ok) {
    console.error(`[jira] ${res.status} ${res.statusText}`);
    process.exit(1);
  }
  return (await res.json()) as JiraField[];
}
