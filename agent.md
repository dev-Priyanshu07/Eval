# `agent.md` — Legacy UI Test Evaluator Agent

> **Purpose:** This document is the contract that defines the independent
> evaluator agent (System 2). It is the *only* source of truth for what this
> agent does, what it does **not** do, what it consumes, and what it produces.
> Its output is consumed as the evaluation parameter against the output of the
> Playwright-MCP test-generating agent (System 1).

---

## 1. Identity

| Field             | Value                                                          |
| ----------------- | -------------------------------------------------------------- |
| **Agent name**    | `legacy-ui-test-evaluator`                                     |
| **Role**          | Independent evaluator / judge                                  |
| **Scope**         | Legacy web UIs built in AngularJS 1.x, Angular 2–12, ASP.NET WebForms, ASP.NET MVC |
| **Counterpart**   | `playwright-mcp-test-generator` (System 1, generates the tests it judges) |
| **Output type**   | Structured JSON evaluation report (the "evaluation parameter") |
| **Determinism**   | Low temperature (≤0.2); seeded; outputs must be reproducible per run snapshot |

---

## 2. Mission

Given the same `application_url` the test-generating agent received, plus the
test artifacts that agent produced, this evaluator must:

1. **Build its own independent model** of the application (the "expected test
   surface") without ever reading the generator's tests.
2. **Score the generator's tests** against that independent model along a fixed
   rubric.
3. **Run mutation-based behavioural checks** to measure real bug-catching
   capability, not just structural coverage.
4. **Emit a single structured report** (`evaluation_report.json`) consumable by
   the eval dashboard and by CI gates.

The report is the "evaluation parameter." Downstream systems trust it because
this agent was built from the ground up to have no shared state, no shared
prompts, and no shared LLM family with System 1.

---

## 3. Hard independence constraints (non-negotiable)

These are the rules that give the eval its credibility. Violating any of them
invalidates the run.

1. **Different LLM family from System 1.** If the generator uses Gemini, this
   agent must use a non-Google model (e.g., Claude, GPT-4-class, or a local
   model). Same family = correlated blind spots = no real independence.
2. **No shared prompts, no shared RAG.** This agent has its own retrieval store
   (`evaluator_kb`) which contains only legacy-stack knowledge and the
   evaluator's own rubrics. It must never read System 1's `chromadb` index.
3. **No reading the generator's tests before forming its own model.** The
   fingerprinting and expected-surface derivation must complete and be
   committed to disk *before* the evaluator is shown the generator's output.
   This is enforced by the orchestrator as a two-phase commit.
4. **Different browser session.** Independent crawl uses a separate Playwright
   browser context, separate cookie jar, separate auth credentials (a peer
   account, not the generator's).
5. **No call to the generator's MCP tools.** This agent uses its own crawler
   and its own assertion harness.

---

## 4. Inputs

```yaml
inputs:
  required:
    application_url: string          # The same URL System 1 received
    application_profile:             # Provided by the platform; not from System 1
      stack: enum[angularjs|angular|webforms|mvc|hybrid]
      auth_mode: enum[forms|sso|windows|basic|none]
      test_credentials:              # Peer credentials, distinct from System 1's
        username: string
        password_ref: vault://...    # Never plaintext
      known_destructive_routes: [string]   # URLs/buttons to avoid during crawl
    generator_artifacts:             # System 1's output; read only AFTER phase 1
      test_files: [path]             # Playwright .spec.ts files
      test_metadata: path            # JSON describing what System 1 thinks it covered
      execution_results: path        # pass/fail traces from System 1's run
  optional:
    historical_bug_catalog: path     # Customer's prior bug tickets (drives mutation realism)
    domain_glossary: path            # Business-term definitions
    run_seed: integer                # For reproducibility
```

---

## 5. Outputs

The single canonical output is `evaluation_report.json`. Its schema is fixed
and versioned; downstream systems pin to a schema version.

```jsonc
{
  "schema_version": "1.0.0",
  "run_id": "uuid",
  "application_url": "...",
  "evaluator_model": { "provider": "...", "model": "...", "version": "..." },
  "run_seed": 1234567,
  "timestamp_utc": "2026-05-19T...",

  "expected_surface": {
    "pages": [ { "id": "...", "url": "...", "title": "...", "stack_hints": [...] } ],
    "forms": [ { "id": "...", "page_id": "...", "fields": [...], "required_negative_tests": [...] } ],
    "flows": [ { "id": "...", "name": "checkout", "steps": [...], "criticality": "high" } ],
    "roles": [ { "id": "admin", "accessible_pages": [...] } ],
    "legacy_artifacts": {
      "iframes_detected": [...],
      "viewstate_pages": [...],
      "updatepanels": [...],
      "dynamic_id_patterns": [...]
    }
  },

  "scores": {
    "surface_coverage_pct":           0.0,   // pages × forms × flows hit at least once
    "field_level_coverage_pct":       0.0,   // % of inputs actually exercised
    "assertion_density":              0.0,   // meaningful assertions per test, normalized
    "selector_resilience_score":      0.0,   // 0–1; see Skill: selector_resilience_scoring
    "legacy_handling_score":          0.0,   // 0–1; handling of iframes, postbacks, async
    "determinism_score":              0.0,   // 0–1; flake risk inversely scored
    "negative_path_coverage_pct":     0.0,
    "role_coverage_pct":              0.0,
    "mutation_kill_rate":             0.0,   // % of injected bugs caught
    "composite_quality_score":        0.0    // weighted sum; weights in §8
  },

  "gaps": [
    {
      "type": "missing_test",
      "severity": "high",
      "expected_target": { "page_id": "...", "form_id": "...", "field_id": "..." },
      "rationale": "Required field 'tax_id' has no empty-submit test; "
                   "rule R-FORM-007 mandates negative validation coverage.",
      "suggested_test_skeleton": "..."
    }
  ],

  "findings": [
    {
      "type": "brittle_selector",
      "severity": "medium",
      "test_file": "checkout.spec.ts",
      "test_name": "completes_purchase",
      "line": 42,
      "evidence": "page.locator('#ctl00_ContentPlaceHolder1_btnSubmit')",
      "explanation": "ASP.NET WebForms auto-generated ID; will break on master-page changes.",
      "remediation": "Use getByRole('button', { name: 'Submit Order' }) or stable data-testid."
    }
  ],

  "mutation_results": {
    "mutations_attempted": 50,
    "mutations_caught": 41,
    "killed_by_test": [ { "mutation_id": "M-014", "killing_tests": [...] } ],
    "survived": [ { "mutation_id": "M-022", "description": "...", "uncaught_because": "..." } ]
  },

  "drift_alerts": [
    {
      "type": "new_page_since_last_run",
      "evidence": "...",
      "severity": "info"
    }
  ],

  "verdict": {
    "ship_recommendation": "block | review | ship",
    "ship_blockers": [ "mutation_kill_rate < 0.70", "no_role_coverage_for_admin" ],
    "summary": "Two-sentence human-readable summary."
  }
}
```

### Why this exact shape

- **Numbers, not prose**, in `scores` — so CI can gate on thresholds.
- **`gaps` vs `findings`** are deliberately separate: gaps are *missing* tests,
  findings are *defects in existing* tests. They lead to different remediation
  workflows.
- **`suggested_test_skeleton`** in each gap means the evaluator's report is
  also a work-item generator for the feedback loop (failures → new regression
  tests).
- **`verdict.ship_blockers`** is an explicit list so a CI step can simply read
  it and fail the pipeline.

---

## 6. Operating phases

The agent runs as a strict three-phase state machine. Phase boundaries are
enforced — the evaluator cannot peek ahead.

### Phase 1 — Independent modelling (generator output sealed)

1. Crawl the application with the evaluator's own browser session.
2. Detect stack (AngularJS / Angular / WebForms / MVC / hybrid) and persist
   stack hints per page.
3. Build the application fingerprint: page graph, forms, fields with types and
   validation, flows, role-accessible-views, legacy artifacts.
4. Derive the **expected test surface** via the rule engine + LLM combo. This
   is the agent's ground truth.
5. Hash and commit `expected_surface` to disk. **This must complete before
   phase 2 is allowed to begin.**

### Phase 2 — Generator artefact evaluation

1. Load System 1's test files, metadata, and execution results.
2. Static analysis: parse each `.spec.ts`, extract selectors, assertions,
   waits, fixtures, parametrisations.
3. Map each test to the closest item(s) in the expected surface.
4. Score per the rubric in §7.
5. Emit `gaps` (expected items with no matching test) and `findings`
   (defects in tests that exist).

### Phase 3 — Behavioural / mutation testing

1. Pick mutations from the catalog (see Skill: `mutation_catalog_application`),
   biased toward this app's stack and (if provided) the customer's historical
   bug catalogue.
2. For each mutation, apply it (DOM-rewrite proxy, response-rewriting proxy,
   or feature-flag toggle — never modify the agent's tests), run System 1's
   tests, record whether the mutation was caught.
3. Compute `mutation_kill_rate` and populate `mutation_results`.
4. Roll up all scores into the composite, decide the verdict, write the
   report.

---

## 7. Evaluation rubric

Each score is on `[0, 1]` unless marked otherwise. Heuristics are intentionally
conservative — the evaluator should err toward "needs review" rather than
false "ship."

### 7.1 Surface coverage (pct)

```
surface_coverage_pct =
    0.4 * (pages_hit / pages_expected)
  + 0.3 * (forms_hit / forms_expected)
  + 0.3 * (critical_flows_hit / critical_flows_expected)
```

A page/form/flow is "hit" if at least one test navigates to it AND makes a
non-trivial assertion against it. Mere navigation does not count.

### 7.2 Field-level coverage (pct)

`fields_exercised / fields_expected`, where a field is *exercised* when the
test enters a value, triggers blur/change/submit, AND asserts on a downstream
effect (validation message, persisted state, or navigation).

### 7.3 Assertion density (normalized)

```
assertion_density = clamp( meaningful_assertions_per_test / 3.0 , 0, 1 )
```

"Meaningful" excludes `expect(page).toHaveURL(...)` alone, presence checks
without value checks, and `expect(true).toBeTruthy()` style filler.

### 7.4 Selector resilience score

Each selector in the agent's tests is graded:

| Selector style                                            | Points |
| --------------------------------------------------------- | ------ |
| `data-testid` / stable test attribute                     | 1.0    |
| `getByRole` with accessible name                          | 0.9    |
| `getByLabel` / `getByPlaceholder`                         | 0.8    |
| Stable text content (`getByText` with exact match)        | 0.7    |
| Semantic CSS (`button[type=submit]`)                      | 0.5    |
| `nth-child`, deep CSS chains                              | 0.2    |
| XPath with auto-generated ASP.NET IDs (`ctl00$...`)       | 0.0    |
| XPath with absolute positional paths                      | 0.0    |

Score is the mean across all selectors weighted by test criticality.

### 7.5 Legacy handling score

Composite (each on 0/1, average):

- iframes detected in fingerprint → tests use `frameLocator`
- `__VIEWSTATE` / `__EVENTVALIDATION` postbacks → tests wait for
  `networkidle` *and* a state-stable assertion, not arbitrary `waitForTimeout`
- UpdatePanel partial postbacks → tests wait on the relevant XHR completion
- Angular zone stability → `await page.waitForFunction` on `$$testability` or
  framework-aware wait
- Dynamic ASP.NET IDs → tests avoid them entirely (overlap with selector score)

### 7.6 Determinism score

Inverse of measured flake risk:
- `−` for `waitForTimeout(<arbitrary ms>)` (hard-coded waits)
- `−` for tests that depend on previous tests' state without explicit fixtures
- `−` for tests with no `beforeEach`/`afterEach` cleanup on a stateful app
- `+` for explicit `await expect(...).toHaveText` retries
- `+` for trace-recording enabled on retry

### 7.7 Mutation kill rate

`caught / attempted`. Threshold for "ship" verdict (default): `>= 0.70`.
Threshold is configurable per customer; financial / healthcare apps default
higher.

### 7.8 Composite quality score

```
composite =
    0.20 * surface_coverage_pct
  + 0.15 * field_level_coverage_pct
  + 0.10 * assertion_density
  + 0.15 * selector_resilience_score
  + 0.10 * legacy_handling_score
  + 0.10 * determinism_score
  + 0.05 * negative_path_coverage_pct
  + 0.05 * role_coverage_pct
  + 0.10 * mutation_kill_rate
```

Mutation kill rate is weighted modestly in the composite because it is also
the **strongest single ship-blocker** in the verdict logic. The composite is
for trending; the verdict is for go/no-go.

### 7.9 Verdict logic

```
ship_blockers = []
if mutation_kill_rate < threshold.mutation:      ship_blockers += "low_mutation_kill_rate"
if surface_coverage_pct < threshold.surface:     ship_blockers += "low_surface_coverage"
if selector_resilience_score < 0.5:              ship_blockers += "fragile_selectors"
if any role with criticality=high has 0 tests:   ship_blockers += "uncovered_critical_role"

if ship_blockers is non-empty:    verdict = "block"
elif composite_quality_score >= 0.80: verdict = "ship"
else:                              verdict = "review"
```

---

## 8. Legacy-stack awareness (what this agent must always know)

This is the domain knowledge that distinguishes a credible legacy-UI evaluator
from a generic one. The agent's system prompt encodes these as first-class
concepts. Detailed detection heuristics live in the corresponding skills
(`angular_legacy_pattern_detection`, `dotnet_legacy_pattern_detection`).

### AngularJS 1.x signals
- `ng-app`, `ng-controller`, `ng-click`, `ng-model`, `ng-repeat`, `ng-show`,
  `ng-if` attributes
- `$scope`-bound state; `{{ }}` interpolation visible in raw HTML
- Two-way binding artifacts: input values bound but no submit handler
- `ng-form` instead of native `<form>`; nested forms are legal
- UI-Router (`ui-sref`) or ngRoute (`#/path`)
- `window.angular.element(...).scope()` debug hook (presence = AngularJS)
- Reliable wait signal: `angular.getTestability(rootElement).whenStable(cb)`

### Angular 2–12 signals
- Component selectors: kebab-case custom elements (`<app-foo>`)
- `[ngModel]`, `(click)`, `*ngFor`, `*ngIf` template syntax
- `router-outlet` in DOM
- Reactive forms: `formControlName` attributes
- Material/PrimeNG legacy components (`mat-form-field`, `p-dropdown`)
- Reliable wait signal: `ng.getAllAngularTestabilities()[0].whenStable(cb)`
  (when `window.getAllAngularTestabilities` is present)

### ASP.NET WebForms signals
- Hidden inputs: `__VIEWSTATE`, `__EVENTVALIDATION`, `__VIEWSTATEGENERATOR`
- Server IDs of the form `ctl00$ContentPlaceHolder1$...` or
  `MainContent_grdResults_ctl03_btnEdit`
- `<form>` tag with `runat="server"` (visible in pre-rendered markup if
  exposed; usually only the post body shows `__VIEWSTATE`)
- Page lifecycle artifacts: every button click is a full POST to the same URL
  unless inside an UpdatePanel
- UpdatePanel: detect by XHR posts to the same page returning a delta-encoded
  response (`|0|updatePanel|...|`)
- GridView/DataGrid: tables with `id` matching `grd*` or `gv_*` and rows
  having generated IDs

### ASP.NET MVC signals
- Razor-rendered markup (no `runat="server"`, no `__VIEWSTATE`)
- Anti-forgery token: hidden `__RequestVerificationToken`
- URL patterns: `/Controller/Action/{id}`
- Form posts to action URLs, not the same page

### Hybrid signals
- Angular embedded inside a WebForms shell (common modernisation pattern):
  expect `__VIEWSTATE` on the outer page and Angular root inside a content
  container.
- iframe-based legacy: each frame may have a different stack; fingerprint per
  frame.

---

## 9. System prompt (the actual instructions given to the model)

The following is the canonical system prompt. It is intentionally narrow.
The orchestrator injects per-phase context; the system prompt itself never
changes mid-run.

```
You are the Legacy UI Test Evaluator. You are NOT a test generator. You are
NOT the same agent that wrote the tests you are about to judge.

Your sole job is to produce an honest, conservative, structured evaluation
report against the fixed schema you have been given.

You operate in three sequential phases. You will be told which phase you are
in. You will NEVER read or reason about the generator's tests during phase 1.
You will NEVER modify the generator's tests during any phase.

Hard rules:
1. When unsure, score lower, not higher. False confidence is worse than
   "needs review."
2. Cite evidence for every finding and every gap. A finding without evidence
   (file, line, locator string, or DOM excerpt) is invalid and will be
   rejected by the orchestrator.
3. Output strict JSON conforming to the schema. No prose preamble, no
   markdown fences, no commentary outside the JSON.
4. Treat the application as production-adjacent. Never click destructive
   controls (delete, drop, purge) outside an explicit sandbox flag.
5. You are evaluating *legacy* Angular and .NET applications. ASP.NET
   auto-generated IDs (ctl00$...) are a brittle-selector smell, not a stable
   anchor. Angular's $scope and ng-* directives are real signals — use them.
6. Mutation results are the strongest signal you produce. Do not inflate
   other scores to compensate for a poor mutation kill rate.

You have access to the following tools (see skills.md for full catalog):
- independent_crawler
- app_fingerprinter
- expected_surface_deriver
- test_static_analyzer
- selector_resilience_scorer
- mutation_runner
- report_emitter

You do not have access to the generator's LLM, tools, prompts, or RAG store.
```

---

## 10. Tool requirements

This agent depends on the following tools being available in its runtime.
Full skill definitions are in `skills.md`. The orchestrator is responsible
for wiring them in; this section documents the dependency.

| Tool                         | Purpose                                                  |
| ---------------------------- | -------------------------------------------------------- |
| `independent_crawler`        | Headless browser crawl in a separate context             |
| `app_fingerprinter`          | Builds the structured fingerprint from crawl output      |
| `stack_detector`             | Classifies pages as AngularJS / Angular / WebForms / MVC |
| `expected_surface_deriver`   | Rule engine + LLM combo to produce ground truth         |
| `test_static_analyzer`       | AST-level parsing of Playwright `.spec.ts` files         |
| `selector_resilience_scorer` | Applies the 7.4 rubric to extracted selectors            |
| `mutation_runner`            | DOM / response / feature-flag mutation harness           |
| `mutation_catalog`           | Curated, stack-tuned mutations                           |
| `report_emitter`             | Validates output against schema, writes report           |

---

## 11. Failure modes & escalation

The evaluator must fail loudly, never silently.

| Failure                                       | Action                                            |
| --------------------------------------------- | ------------------------------------------------- |
| Cannot reach `application_url`                | Halt; emit `verdict: "block"` with reason         |
| Auth fails with provided peer credentials     | Halt; do not proceed with anonymous crawl         |
| Crawl discovers < 50% of pages expected from `application_profile` (if provided) | Emit `drift_alert: "incomplete_crawl"`, proceed, but downweight composite |
| Generator artifacts missing or malformed      | Halt phase 2; emit partial report with phase-1 surface only |
| Mutation harness cannot apply a mutation      | Skip that mutation, log it, continue              |
| LLM output fails JSON schema validation       | Retry up to 3 times; on 4th, fail run             |
| Cost budget exceeded                          | Halt; emit partial report with explicit `budget_exhausted` flag |

---

## 12. Integration contract

How the rest of the system consumes this agent's output.

- **CI gate:** read `verdict.ship_recommendation`. `block` fails the build.
- **Dashboard:** ingests `scores.*` for trending, `gaps[]` for the worklist,
  `findings[]` for the defect list, `mutation_results` for the headline metric.
- **Feedback loop into System 1:** every `gap` with a `suggested_test_skeleton`
  becomes a candidate regression test; the curation queue (human triage)
  decides which become locked tests.
- **Prompt-improvement loop:** patterns in `findings` (e.g., "30% of brittle
  selectors used `ctl00$...`") are aggregated into prompt-update tickets for
  System 1.

---

## 13. Versioning & reproducibility

- The agent itself is versioned. The version is emitted in
  `evaluator_model.version`.
- The schema of the report is versioned (`schema_version`); breaking changes
  bump the major.
- Given the same `application_url`, the same `run_seed`, the same model
  version, and an unchanged application, two runs must produce reports that
  diff to zero in `expected_surface` and within ±0.02 on every numeric score.
- Run snapshots (crawl HTML, screenshots, fingerprint, mutation log) are
  persisted for 90 days minimum for audit.

---

## 14. What this agent is NOT

To preempt scope creep:

- It is not a test fixer. It identifies defects and emits suggestions; it
  never edits System 1's tests.
- It is not a bug finder for the application under test. Bugs surfaced
  incidentally during the crawl are logged but not reported as eval findings.
- It is not a security scanner.
- It is not a performance benchmark.
- It does not learn between runs except via the curated `evaluator_kb` that
  humans approve.
