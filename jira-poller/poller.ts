import { config } from "./src/config.ts";
import {
  listDevelopersCmd,
  listFieldsCmd,
  queryCmd,
  runLoop,
  runOnce,
} from "./src/commands.ts";

const { cli, jira, notifier, pollIntervalMs } = config;

console.log(
  `[boot] jira-poller → ${notifier.url} ` +
    `(status="${jira.targetStatus}", assignee=${jira.assignee}, ` +
    `interval=${pollIntervalMs}ms, mode=${cli.kind})`,
);

switch (cli.kind) {
  case "list-fields":
    await listFieldsCmd(cli.filter);
    process.exit(0);
  case "list-developers":
    await listDevelopersCmd();
    process.exit(0);
  case "query":
    await queryCmd(cli.jql);
    process.exit(0);
  case "once":
    await runOnce();
    process.exit(0);
  case "loop":
    await runLoop();
    break;
}
