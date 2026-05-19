# `skills.md` — Skills Catalogue for `legacy-ui-test-evaluator`

> Each skill below is a self-contained capability the agent loads when needed.
> Skills are deterministic where possible (rule engines, AST parsers) and
> LLM-assisted only where genuinely required (ambiguity resolution, ground
> truth derivation). The agent.md contract refers to these by name.

---

## Index

| #  | Skill name                            | Phase | LLM-assisted? |
| -- | ------------------------------------- | ----- | ------------- |
| 1  | `independent_app_fingerprinting`      | 1     | partial       |
| 2  | `stack_detection`                     | 1     | rule-based    |
| 3  | `angular_legacy_pattern_detection`    | 1     | rule-based    |
| 4  | `dotnet_legacy_pattern_detection`     | 1     | rule-based    |
| 5  | `expected_test_surface_derivation`    | 1     | yes           |
| 6  | `test_static_analysis`                | 2     | rule-based    |
| 7  | `selector_resilience_scoring`         | 2     | rule-based    |
| 8  | `assertion_quality_assessment`        | 2     | partial       |
| 9  | `coverage_gap_analysis`               | 2     | rule-based    |
| 10 | `mutation_catalog_application`        | 3     | rule-based    |
| 11 | `mutation_execution_and_kill_scoring` | 3     | rule-based    |
| 12 | `comparative_scoring_and_verdict`     | 3     | rule-based    |
| 13 | `report_emission_and_validation`      | 3     | none          |

---

## Skill 1 — `independent_app_fingerprinting`

**When to use:** Phase 1, immediately after the agent receives the URL and
peer credentials.

**What it does:** Performs an exhaustive crawl of the application using a
fresh Playwright browser context (separate from System 1's), captures every
discoverable page, persists raw HTML and the post-render DOM, and produces a
normalised `crawl_artifact` for downstream skills.

**Why this skill exists:** The independence of the evaluator depends on this
crawl never reusing System 1's exploration. Different crawl strategy →
different blind spots → genuine cross-check.

**Inputs**
- `application_url`
- `peer_credentials`
- `known_destructive_routes` (deny-list)
- `crawl_depth_max` (default 5)
- `crawl_time_budget` (default 20 minutes)

**Outputs**
```jsonc
{
  "pages": [
    {
      "id": "p_001",
      "url": "...",
      "title": "...",
      "html_snapshot_path": "...",
      "rendered_dom_path": "...",
      "accessibility_tree_path": "...",
      "screenshot_path": "...",
      "discovered_from": "p_000",
      "interactive_elements_count": 14
    }
  ],
  "navigation_graph": [ { "from": "p_001", "to": "p_002", "trigger": "click button[name=Continue]" } ]
}
```

**How it differs from a System 1 crawler**
- Uses **BFS** on the link graph (not LLM-driven exploration). System 1's
  agent decides where to go next using reasoning; this skill is mechanical.
- Captures the **accessibility tree** alongside the DOM. Many legacy apps
  have cleaner ATs than DOMs.
- Always logs out and back in between major sections to detect role-gated
  pages.
- Hard stop on any URL or button whose accessible name matches the
  destructive-action allowlist regex (`/delete|drop|purge|wipe|reset/i`)
  unless flagged sandbox.

**Failure modes**
- Infinite redirect → cap at 10 hops, mark page as `unreachable`.
- Auth wall mid-crawl → re-authenticate up to 3 times, then halt section.
- Iframe with cross-origin content → record `iframe_blocked` artifact;
  fingerprint what we can from the parent frame.

---

## Skill 2 — `stack_detection`

**When to use:** During fingerprinting, applied per page.

**What it does:** Classifies each crawled page as one of
`{ angularjs, angular, webforms, mvc, hybrid, unknown }`, with a confidence
score and the evidence that drove the call.

**Inputs:** A page's rendered DOM + raw HTML + a 2-second JS-execution
sandbox (to probe `window.angular`, `window.ng`, etc.).

**Detection rules (in priority order, first match wins):**

| Stack       | Primary evidence                                                                       | Secondary                                          |
| ----------- | -------------------------------------------------------------------------------------- | -------------------------------------------------- |
| `angularjs` | `window.angular` defined AND `angular.version.major === 1`                             | `ng-app`/`ng-controller` in DOM, `{{ }}` text      |
| `angular`   | `window.getAllAngularTestabilities` defined OR `ng-version` attribute on any element   | Component-style custom elements (`<app-*>`)        |
| `webforms`  | Hidden input `__VIEWSTATE` AND form with `action` matching same page                   | IDs matching `/^ctl\d{2}\$/` or `/^[A-Za-z]+_ctl\d/` |
| `mvc`       | Hidden input `__RequestVerificationToken` AND no `__VIEWSTATE`                         | URL pattern `/Controller/Action`                   |
| `hybrid`    | Any combination of the above on the same page                                          | Often Angular inside a WebForms shell              |
| `unknown`   | None of the above match with confidence ≥ 0.6                                          | —                                                  |

**Output**
```jsonc
{
  "page_id": "p_001",
  "stack": "webforms",
  "confidence": 0.95,
  "evidence": [
    "Hidden input __VIEWSTATE present (length 4823)",
    "Submit button id 'ctl00$ContentPlaceHolder1$btnSave' matches ASP.NET pattern"
  ]
}
```

**Why rule-based, not LLM:** Stack detection is a closed-set classification
with cheap, unambiguous signals. An LLM here is slower, more expensive, and
strictly less accurate.

---

## Skill 3 — `angular_legacy_pattern_detection`

**When to use:** On any page classified `angularjs` or `angular` by Skill 2.

**What it does:** Inventories Angular-specific patterns that affect test
strategy and wait semantics.

**Patterns detected**

For **AngularJS 1.x**:
- `ng-click`, `ng-model`, `ng-submit`, `ng-repeat`, `ng-if`, `ng-show`,
  `ng-hide` directives
- Custom directive usage (any element with a kebab-cased attribute not in the
  HTML spec)
- `ng-form` nesting depth
- `$http` / `$resource` call surfaces (via network observation during crawl)
- Routing mode: `ui-router` vs `ngRoute`
- Testability hook availability:
  `angular.getTestability(rootElement).whenStable(cb)`

For **Angular 2+**:
- `*ngFor`, `*ngIf`, `[ngModel]`, `(click)`, `[formControl]`,
  `[formControlName]`
- Reactive forms vs template-driven forms
- Material / PrimeNG / Kendo legacy components by tag prefix
- `router-outlet` presence and route changes during crawl
- Zone stability hook availability:
  `window.getAllAngularTestabilities()[0].whenStable(cb)`

**Why this skill matters for evaluation:** When Skill 7 scores selector
resilience and Skill 8 scores assertion quality, the evaluator must know
whether the framework provides a `whenStable` hook. A test that uses
arbitrary `waitForTimeout` on an Angular page is downgraded; the framework
*offers* a deterministic wait and the test isn't using it.

**Output** appended to each page's fingerprint under
`legacy_artifacts.angular_patterns`.

---

## Skill 4 — `dotnet_legacy_pattern_detection`

**When to use:** On any page classified `webforms`, `mvc`, or `hybrid` by
Skill 2.

**What it does:** Inventories ASP.NET-specific patterns that affect test
strategy.

**Patterns detected**

For **WebForms**:
- `__VIEWSTATE` size (large viewstate → expensive postbacks, slow tests)
- `__EVENTVALIDATION` presence (strict validation → tests must not strip
  hidden fields)
- Server-control ID patterns (`ctl00$ContentPlaceHolder1$...`,
  `MainContent_grdResults_ctl03_btnEdit`)
- UpdatePanel presence (detect via partial-postback XHRs and the
  `MicrosoftAjax` framework script)
- GridView/DataGrid controls (tables with row IDs auto-generated per record)
- Page lifecycle: full postback vs partial postback per interactive control
- Cross-page postbacks (button `PostBackUrl` attribute referencing another
  `.aspx`)

For **MVC**:
- Anti-forgery token presence and its hidden field name
- Action URL patterns
- Unobtrusive validation: `data-val`, `data-val-required`, `data-val-regex`
  attributes (these are GIFTS — they reveal expected validation rules
  declaratively, and Skill 5 uses them heavily)

**Why this skill matters:** ASP.NET WebForms is the single most fragile
target for naive test generation. The auto-generated IDs are the most
common brittle-selector smell. The hidden fields are the most common cause
of "test passes locally, fails in CI" flakes. This skill makes the
evaluator brutally accurate about both.

**Output** appended to each page's fingerprint under
`legacy_artifacts.dotnet_patterns`.

---

## Skill 5 — `expected_test_surface_derivation`

**When to use:** Phase 1, after fingerprint is complete. **This is the only
LLM-heavy skill in phase 1.**

**What it does:** Takes the full fingerprint and produces the
`expected_surface` — the set of pages, forms, fields, flows, and roles that
a competent test suite *should* cover. This is the ground truth.

**Two-stage derivation**

**Stage A — Deterministic rules** (run first, output cached as the
"floor"):
- Every form with at least one `required` field → must have a
  `submit_empty_form` test (negative path) AND a `submit_valid_form` test.
- Every form field with a `data-val-regex` (MVC) or HTML5 `pattern`
  attribute → must have a `boundary_invalid_input` test.
- Every navigation link in the navigation graph → must be reachable by at
  least one test.
- Every role discovered during the role-segmented crawl → must have at least
  one role-specific test.
- Every page with a destructive action (deny-list trigger) → must have a
  `cancel_destructive_action` test even if not the destructive action itself.

**Stage B — LLM-assisted flow synthesis** (uses the rules output + the
navigation graph + page titles/headings):
- The LLM proposes high-level flows ("checkout", "user onboarding",
  "report generation"). For each, it lists ordered steps in terms of pages
  and forms already in the fingerprint.
- The LLM proposes a `criticality` (high/medium/low) for each flow based on
  business-impact heuristics (financial, security, data-integrity = high).
- The LLM **may not invent** pages, forms, or fields that are not in the
  fingerprint. The orchestrator validates this and rejects any output that
  references unknown IDs.

**Output**
```jsonc
{
  "pages":  [ { ...from fingerprint, plus required_test_types: [...] } ],
  "forms":  [ { ...from fingerprint, plus required_negative_tests: [...] } ],
  "flows":  [ { "id": "f_001", "name": "checkout", "criticality": "high",
                "steps": [ { "page_id": "p_001", "action": "..." }, ... ] } ],
  "roles":  [ { "id": "admin", "accessible_pages": [...],
                "exclusive_pages": [...] } ]
}
```

**Why a two-stage design:** The deterministic floor guarantees that
unambiguous coverage requirements (every required field gets a negative
test) are never missed due to LLM whim. The LLM stage adds flow-level
reasoning that rules alone can't express.

---

## Skill 6 — `test_static_analysis`

**When to use:** Phase 2, immediately after the generator's artifacts are
unsealed.

**What it does:** Parses each Playwright `.spec.ts` file into a normalised
test-AST representation. **Pure static analysis — does not execute the
tests.**

**What it extracts per test**
- Test name and describe-block path
- Setup / teardown fixtures used
- All locators (selector strings + the API used: `locator`, `getByRole`,
  `getByText`, `getByLabel`, etc.)
- All actions (`click`, `fill`, `selectOption`, `check`, `press`)
- All assertions (`expect(...).toX(...)`)
- All waits (categorised: `waitForSelector`, `waitForResponse`,
  `waitForTimeout`, `waitForLoadState`)
- All navigations (`page.goto`, `page.click` on links)
- Test parametrisation (`test.each`-style loops)
- Test dependencies (does this test depend on another test's side effects?)

**Output**
```jsonc
{
  "tests": [
    {
      "file": "checkout.spec.ts",
      "name": "completes_purchase_with_valid_card",
      "describe_path": ["Checkout", "Happy Path"],
      "locators": [
        { "line": 12, "api": "getByRole", "args": ["button", { "name": "Add to Cart" }] },
        { "line": 18, "api": "locator", "args": ["#ctl00_ContentPlaceHolder1_btnPay"] }
      ],
      "actions": [...],
      "assertions": [
        { "line": 26, "api": "toHaveURL", "args": ["/order-confirmation"] }
      ],
      "waits": [
        { "line": 22, "api": "waitForTimeout", "args": [3000], "smell": "arbitrary_wait" }
      ],
      "uses_fixtures": ["authenticatedPage"],
      "depends_on_test_state": false
    }
  ]
}
```

**Why static, not dynamic:** The generator already ran the tests and gave us
the execution results. Re-running them here adds nothing. Static parsing is
fast, deterministic, and lets us reason about *what the tests intend*, which
the execution log doesn't capture.

**Implementation note:** Use `@babel/parser` with TypeScript plugin or
`ts-morph`. Do not use regex on test source — Playwright's chaining
(`page.getByRole(...).first().click()`) is too easy to misparse.

---

## Skill 7 — `selector_resilience_scoring`

**When to use:** Phase 2, after Skill 6 has extracted all locators.

**What it does:** Applies the rubric in agent.md §7.4 to every locator from
every test. Produces both a per-test score and a per-suite score.

**Scoring table (canonical)**

| Locator pattern                                                | Score |
| -------------------------------------------------------------- | ----- |
| `getByTestId(...)` or any `data-testid`-based selector         | 1.0   |
| `getByRole(role, { name })` with non-empty name                | 0.9   |
| `getByLabel(...)`                                              | 0.85  |
| `getByPlaceholder(...)`                                        | 0.75  |
| `getByText(exact: true, ...)`                                  | 0.7   |
| `getByText(...)` without exact match                           | 0.5   |
| CSS with semantic anchors (`button[type=submit]`, `nav a`)     | 0.5   |
| `locator('id^="ctl00"')` or any `ctl\d+` substring             | 0.0   |
| `locator('id^="MainContent_"')` (ASP.NET master-page prefix)   | 0.1   |
| XPath with `[1]`, `[2]`-style positional predicates            | 0.1   |
| CSS with `:nth-child` / `:nth-of-type`                         | 0.2   |
| CSS chains > 4 segments deep                                   | 0.3   |
| Class selectors only (`.btn.primary`)                          | 0.4   |
| Selectors targeting Angular's `$$0`-style debug attributes     | 0.0   |

**Special-case bonuses and penalties**
- `+0.1` if the test uses `getByRole` *and* fallback chain
  (`.or()` / try-locator pattern). Resilient by construction.
- `−0.2` for any selector inside a `frameLocator` chain where the iframe is
  referenced by index (`frameLocator('iframe').nth(2)`) instead of name/title.
- `−0.3` if a selector matches a pattern in the page's
  `legacy_artifacts.dotnet_patterns.unstable_id_patterns` list AND no
  fallback is used.

**Suite-level score:** Mean of per-test scores, weighted by the criticality
of the flow each test belongs to (high = 3x, medium = 2x, low = 1x).

**Output:** Per-test scores feed `findings[]` (any locator scoring < 0.5
becomes a `brittle_selector` finding with line number and remediation).
Suite-level score feeds `scores.selector_resilience_score`.

---

## Skill 8 — `assertion_quality_assessment`

**When to use:** Phase 2, alongside Skill 7.

**What it does:** Judges whether each test's assertions are *meaningful* —
whether they actually verify intended behaviour, or just confirm the page
didn't crash.

**Heuristic rules (deterministic part)**

| Assertion pattern                                  | Quality |
| -------------------------------------------------- | ------- |
| `toHaveText(exact)` on a state-bearing element     | high    |
| `toHaveValue` on a form field after a side-effect  | high    |
| `toHaveURL` *combined with* another assertion       | high    |
| `toHaveURL` alone as the only assertion             | low     |
| `toBeVisible` / `toBeAttached` *alone*              | low     |
| `toHaveCount(>0)` without specificity              | low     |
| `expect(true).toBeTruthy()` or similar             | invalid |
| Snapshot assertions (`toMatchSnapshot`)            | medium  |
| Network response shape assertions                   | high    |

**LLM-assisted check (only for ambiguous cases):** Where a test makes 3+
assertions but none individually score "high," the LLM is asked to judge
whether the *combination* is meaningful. Output is binary
(`coherent_assertion_group: true|false`) with one-sentence rationale.

**Output**
- `scores.assertion_density` (normalised average per test)
- `findings[]` entries for any test whose strongest assertion is `low` or
  whose total meaningful-assertion count is zero.

---

## Skill 9 — `coverage_gap_analysis`

**When to use:** Phase 2, after Skills 6–8 have produced the test-AST and
scores.

**What it does:** Performs a left-outer join: every item in
`expected_surface` is matched against the set of generator tests that
touched it. Items with no match become `gaps`.

**Matching rules**

A test "covers" an expected-surface item if:
- For a page: the test navigates to its URL AND makes ≥ 1 assertion on the
  page after navigation.
- For a form: the test fills ≥ 50% of the form's required fields AND
  submits AND asserts on the post-submit state.
- For a flow: the test executes the flow's steps in order (with optional
  intermediate steps allowed) AND asserts on the final state.
- For a role: the test runs under the role's auth context (per fixture) AND
  asserts something role-specific (visible-to-admin element, etc.).

**Severity assignment**
- High: gap on a high-criticality flow, or on any role-exclusive page, or
  on any form with destructive consequences.
- Medium: gap on a medium-criticality flow, or on any required-field
  negative-test rule.
- Low: gap on a low-criticality flow or informational page.

**Suggested test skeleton generation**

For each gap, this skill emits a Playwright test skeleton tailored to the
detected stack. Examples:

For an Angular form gap:
```typescript
test('submits valid_user_profile_form successfully', async ({ page }) => {
  await page.goto('/profile/edit');
  await page.waitForFunction(() =>
    (window as any).getAllAngularTestabilities?.()[0]?.isStable());
  await page.getByLabel('First name').fill('Jane');
  await page.getByLabel('Last name').fill('Doe');
  await page.getByRole('button', { name: 'Save' }).click();
  await expect(page.getByText('Profile updated')).toBeVisible();
});
```

For a WebForms gap (note the postback-aware wait):
```typescript
test('cancel_button_returns_to_listing_without_save', async ({ page }) => {
  await page.goto('/Customers/Edit.aspx?id=42');
  await page.getByLabel('Notes').fill('test edit');
  // Cancel triggers a full postback; wait on networkidle + a target-page anchor
  await Promise.all([
    page.waitForLoadState('networkidle'),
    page.getByRole('button', { name: 'Cancel' }).click(),
  ]);
  await expect(page).toHaveURL(/Customers\/List\.aspx/);
  await expect(page.getByRole('cell', { name: 'test edit' })).toHaveCount(0);
});
```

The skeleton is deliberately incomplete (no test data fixtures, no
edge-case assertions) — it's a starting point for the curation queue, not a
finished test.

---

## Skill 10 — `mutation_catalog_application`

**When to use:** Phase 3 setup.

**What it does:** Selects mutations from the catalog appropriate for this
application's stack and history, producing a deterministic mutation list
for this run.

**Catalog structure**

The catalog (`mutations.yaml`) groups mutations by stack and type:

```yaml
mutations:
  - id: M-ANG-001
    stack: [angular, angularjs]
    type: dom_rewrite
    description: "Remove ng-required from a randomly chosen required input"
    target_selector: "input[ng-required], input[required]"
    mutator: "remove_attribute"
    expected_break: "Form should submit empty when it shouldn't; valid tests catch this"

  - id: M-NET-007
    stack: [webforms]
    type: response_rewrite
    description: "Strip __EVENTVALIDATION from server response on form pages"
    mutator: "regex_replace"
    pattern: "<input[^>]*name=\"__EVENTVALIDATION\"[^>]*>"
    replacement: ""
    expected_break: "Form posts should be rejected by server; tests asserting success should fail"

  - id: M-NET-014
    stack: [webforms, mvc]
    type: feature_flag
    description: "Loosen email-format validation regex to allow obviously invalid emails"
    expected_break: "Negative validation tests should fail"

  - id: M-GEN-022
    stack: [any]
    type: dom_rewrite
    description: "Change a submit button's text from 'Save' to 'Submit'"
    mutator: "text_replace"
    expected_break: "Tests using getByRole('button', { name: 'Save' }) should fail; tests using IDs may pass spuriously"
```

**Selection rules**
- Always include 100% of mutations matching the app's detected stack.
- Always include all `stack: [any]` mutations.
- If `historical_bug_catalog` was provided, augment with synthetic
  mutations derived from real bug patterns (this is a separate skill, not
  in v1 scope; placeholder).
- Cap at 50 mutations per run by default; configurable.
- Deterministic ordering by mutation ID + run seed.

**Output:** `mutation_plan.json` listing the selected mutations with their
parameters.

**Why this matters:** Random or naive mutations produce noisy mutation
kill rates. Stack-targeted, historically-grounded mutations produce a
kill rate that QA leadership can defend to their auditors.

---

## Skill 11 — `mutation_execution_and_kill_scoring`

**When to use:** Phase 3, after Skill 10.

**What it does:** For each mutation in the plan, applies it, runs System
1's full test suite against the mutated app, records pass/fail, then
reverts the mutation before the next.

**Mutation application mechanisms (in order of preference)**

1. **Response-rewriting proxy** (mitmproxy-based) for mutations of type
   `response_rewrite`. Cleanest — the app itself is untouched, only what
   the browser sees changes.
2. **Feature flags** for mutations of type `feature_flag`. Requires
   cooperation from the application; only usable if the customer exposes a
   flag harness.
3. **In-page DOM rewriting** via Playwright `page.addInitScript` for
   mutations of type `dom_rewrite`. Last resort because some mutations
   (server-side validation changes) cannot be expressed this way.

**Kill criteria**
- A mutation is "killed" if AT LEAST ONE test in System 1's suite fails
  under it that passed without it.
- A mutation that no tests fail on is "survived."
- A mutation that breaks the test infrastructure itself (not a real catch)
  is "errored" and excluded from the kill rate denominator.

**Output**
```jsonc
{
  "mutations_attempted": 50,
  "mutations_caught": 41,
  "mutations_errored": 2,
  "kill_rate": 41 / 48,
  "killed_by_test": [ { "mutation_id": "M-NET-007", "killing_tests": ["checkout.spec.ts:completes_purchase"] } ],
  "survived": [
    { "mutation_id": "M-GEN-022",
      "description": "Submit button text changed Save→Submit",
      "uncaught_because": "All tests targeted button via id='ctl00$...btnSave', not by text" }
  ]
}
```

**Survival diagnostics are gold.** Each survived mutation is also a
finding — it tells the curation queue *exactly* what kind of regression
the test suite would miss in production.

---

## Skill 12 — `comparative_scoring_and_verdict`

**When to use:** Phase 3, final step before report emission.

**What it does:** Aggregates every score from earlier skills into the
composite, applies verdict logic, and produces the headline numbers.

**Inputs**
- All scores from Skills 7, 8, 9, 11
- Thresholds from configuration (per-customer overridable)

**Logic:** Implements the formulas and verdict tree in agent.md §7.8–7.9
exactly as specified. Deterministic — given the same inputs, always
produces the same outputs.

**Threshold defaults (overridable per customer)**

| Threshold                       | Default | Healthcare / Finance |
| ------------------------------- | ------- | -------------------- |
| `threshold.mutation`            | 0.70    | 0.85                 |
| `threshold.surface`             | 0.80    | 0.90                 |
| `threshold.composite_for_ship`  | 0.80    | 0.90                 |
| `threshold.selector_resilience` | 0.50    | 0.65                 |

---

## Skill 13 — `report_emission_and_validation`

**When to use:** The very last step of phase 3.

**What it does:** Assembles the final report JSON, validates it against the
schema (`evaluation_report.schema.json`, v1.0.0), writes it to the run
artifact directory, and signals the orchestrator.

**Validation rules**
- Every `finding` must include `evidence` (file + line OR locator string OR
  DOM excerpt).
- Every `gap` must include `expected_target` referencing an ID present in
  `expected_surface`.
- Every numeric score must be in `[0, 1]` (except `_pct` fields which may
  be on `[0, 100]` per local convention).
- `verdict.ship_recommendation` must be one of `block | review | ship` and
  must be consistent with `ship_blockers`.

**Failure handling**
- On validation failure, do NOT emit a partial report under the canonical
  name. Emit it under `evaluation_report.invalid.json` and signal
  orchestrator with an error code.
- The orchestrator may retry skill 13 up to 3 times if the failure is a
  transient JSON-serialisation issue. After that, the run fails.

**Why this skill is deliberately dumb:** Report emission is the most
load-bearing handoff in the system. CI, dashboards, and feedback loops
all depend on this file being well-formed. The skill does no reasoning,
no LLM calls — just assembly, schema validation, write, signal.

---

## Cross-skill invariants

These rules apply to every skill above. They are restated here because
violations are how independence breaks down in practice.

1. **No skill in phase 1 may read any artifact produced by System 1.** The
   orchestrator enforces this via filesystem ACLs on the run directory.
2. **No skill may call the System-1 LLM.** This is enforced by network
   policy: the evaluator's runtime has no credentials for System 1's
   model provider.
3. **No skill may modify System 1's test files.** Skills produce findings
   and suggestions; the curation queue (human-in-the-loop) decides what,
   if anything, becomes a real test.
4. **Every skill emits structured output validated against its sub-schema.**
   No skill returns free-form prose into the run pipeline.
5. **Every LLM call inside a skill records its prompt, response, model
   version, and token cost** to the run's telemetry sidecar. Every dollar
   spent on this evaluator is auditable.
6. **Skills are independently testable.** Each ships with a small fixture
   suite (golden inputs → golden outputs) that runs in CI on every change
   to the skill.

---

## Skill maturity & roadmap

| Skill                                   | v1 state    | Planned v2                                              |
| --------------------------------------- | ----------- | ------------------------------------------------------- |
| `independent_app_fingerprinting`        | production  | Add OCR fallback for canvas-rendered legacy widgets     |
| `stack_detection`                       | production  | Detect Knockout.js, Backbone for fuller legacy coverage |
| `angular_legacy_pattern_detection`      | production  | Add Ionic / Cordova hybrid detection                    |
| `dotnet_legacy_pattern_detection`       | production  | Add SharePoint / Telerik pattern detection              |
| `expected_test_surface_derivation`      | production  | Incorporate customer-provided BRDs / user stories       |
| `test_static_analysis`                  | production  | Support Cypress / Selenium for non-Playwright suites    |
| `selector_resilience_scoring`           | production  | Per-customer custom selector rules                      |
| `assertion_quality_assessment`          | production  | Property-based assertion strength detection             |
| `coverage_gap_analysis`                 | production  | Cross-page state-machine coverage analysis              |
| `mutation_catalog_application`          | production  | Auto-mine mutations from customer Jira history          |
| `mutation_execution_and_kill_scoring`   | production  | Parallel mutation execution across worker pool          |
| `comparative_scoring_and_verdict`       | production  | Bayesian confidence intervals on each score             |
| `report_emission_and_validation`        | production  | Schema v2 with backwards-compatibility shim             |
