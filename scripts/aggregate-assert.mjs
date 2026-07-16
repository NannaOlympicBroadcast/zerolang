import nodeAssert from "node:assert/strict";
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname } from "node:path";

const methods = ["deepEqual", "doesNotMatch", "equal", "match", "notEqual", "ok"];

function formatError(error) {
  if (!(error instanceof Error)) return String(error);
  return error.stack || error.message;
}

function formatFailure(failure, index) {
  const header = `${index + 1}. ${failure.method}: ${failure.message}`;
  return failure.stack ? `${header}\n${failure.stack}` : header;
}

export function createAggregateAssert(options = {}) {
  const failFast = process.env.ZERO_VALIDATION_FAIL_FAST === "1" ||
    process.env.ZERO_ASSERT_FAIL_FAST === "1" ||
    options.failFast === true;
  const failures = [];

  function record(method, error) {
    if (failFast) throw error;
    failures.push({
      method,
      message: error instanceof Error ? error.message : String(error),
      stack: error instanceof Error ? error.stack : "",
    });
  }

  function aggregateAssert(value, message) {
    try {
      nodeAssert(value, message);
    } catch (error) {
      record("assert", error);
    }
  }

  for (const method of methods) {
    aggregateAssert[method] = (...args) => {
      try {
        nodeAssert[method](...args);
      } catch (error) {
        record(method, error);
      }
    };
  }

  aggregateAssert.fail = (...args) => {
    try {
      nodeAssert.fail(...args);
    } catch (error) {
      record("fail", error);
    }
  };

  aggregateAssert.failures = failures;
  return aggregateAssert;
}

export function finishAggregateAssert(assert, options = {}) {
  const failures = assert?.failures ?? [];
  if (failures.length === 0) return;

  const suite = options.suite || "validation";
  const reportPath = options.reportPath;
  const summary = failures.map(formatFailure).join("\n\n");
  const text = `${suite} collected ${failures.length} assertion failure(s):\n\n${summary}\n`;

  if (reportPath) {
    mkdirSync(dirname(reportPath), { recursive: true });
    writeFileSync(reportPath, `${JSON.stringify({ suite, ok: false, failures }, null, 2)}\n`);
  }

  process.stderr.write(text);
  process.exit(1);
}

export function describeFailure(error) {
  return formatError(error);
}
