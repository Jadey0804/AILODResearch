import fs from "node:fs/promises";
import crypto from "node:crypto";
import { SpreadsheetFile, Workbook } from "@oai/artifact-tool";

const projectRoot = "C:/WarwickProjects/AILODResearch";
const outputDir = `${projectRoot}/outputs/01a05060-81af-7bc2-8300-7e4999a3edc9`;
const outputPath = `${outputDir}/AILOD_Phase8_Formal_Results_Analysis_CN_v1.1.xlsx`;
const supportDir = "C:/Users/admin/.codex/visualizations/2026/08/30/01a05060-81af-7bc2-8300-7e4999a3edc9/phase8_analysis_v11";
const analysisPath = `${outputDir}/AILOD_Phase8_Formal_Analysis_Data_v1.1.json`;

const rawSources = [
  ["Accuracy Raw", "D:/AILODFormal/Phase8/FormalAccuracy-v1/metrics_summary.csv"],
  ["Perf 2k Raw", "D:/AILODFormal/Phase8/FormalPerformance-v1/N2000/metrics_summary.csv"],
  ["Perf 10k Raw", "D:/AILODFormal/Phase8/FormalPerformance-v1/N10000/metrics_summary.csv"],
  ["Perf 20k Raw", "D:/AILODFormal/Phase8/FormalPerformance-v1/N20000/metrics_summary.csv"],
];

function sha256(buffer) {
  return crypto.createHash("sha256").update(buffer).digest("hex").toUpperCase();
}

function columnName(index) {
  let value = index + 1;
  let result = "";
  while (value > 0) {
    const remainder = (value - 1) % 26;
    result = String.fromCharCode(65 + remainder) + result;
    value = Math.floor((value - 1) / 26);
  }
  return result;
}

function rangeAddress(startRow, startColumn, rowCount, columnCount) {
  return `${columnName(startColumn)}${startRow + 1}:${columnName(startColumn + columnCount - 1)}${startRow + rowCount}`;
}

function styleTitle(sheet, address) {
  sheet.getRange(address).format = {
    fill: "#1F4E78",
    font: { bold: true, color: "#FFFFFF", size: 16 },
    verticalAlignment: "center",
  };
}

function styleSection(sheet, address) {
  sheet.getRange(address).format = {
    fill: "#D9EAF7",
    font: { bold: true, color: "#1F1F1F", size: 11 },
    borders: { bottom: { style: "thin", color: "#8EA9C1" } },
  };
}

function styleHeader(sheet, address) {
  sheet.getRange(address).format = {
    fill: "#4472C4",
    font: { bold: true, color: "#FFFFFF" },
    wrapText: true,
    verticalAlignment: "center",
    borders: { bottom: { style: "thin", color: "#2F5597" } },
  };
}

function setWidths(sheet, widths) {
  widths.forEach((width, index) => {
    sheet.getRange(`${columnName(index)}:${columnName(index)}`).format.columnWidth = width;
  });
}

function writeTable(sheet, startRow, startColumn, headers, rows) {
  const headerRange = rangeAddress(startRow, startColumn, 1, headers.length);
  sheet.getRange(headerRange).values = [headers];
  styleHeader(sheet, headerRange);
  if (rows.length) {
    sheet.getRange(rangeAddress(startRow + 1, startColumn, rows.length, headers.length)).values = rows;
  }
}

function orderedValues(row, fields) {
  return fields.map((field) => row[field] ?? null);
}

const payload = JSON.parse((await fs.readFile(analysisPath, "utf8")).replace(/^\uFEFF/, ""));
const workbook = Workbook.create();

const readme = workbook.worksheets.add("README");
const headline = workbook.worksheets.add("Headline");
const accuracyStats = workbook.worksheets.add("Accuracy Stats");
const pairedSeeds = workbook.worksheets.add("Accuracy Paired Seeds");
const seedPlots = workbook.worksheets.add("Seed Plots");
const thresholds = workbook.worksheets.add("Threshold Audit");
const taskActive = workbook.worksheets.add("TaskActive");
const performanceStats = workbook.worksheets.add("Performance Stats");
const performanceRuns = workbook.worksheets.add("Performance Runs");
const performanceOrder = workbook.worksheets.add("Performance Order");
const qc = workbook.worksheets.add("QC");

readme.getRange("A1:F1").values = [["AILOD Phase 8 正式结果工作簿 v1.1", null, null, null, null, null]];
styleTitle(readme, "A1:F1");
readme.getRange("A3:B15").values = [
  ["用途", "汇总 480 次准确性运行和 90 次性能运行，并保留逐 Seed 与原始 CSV 证据。"],
  ["冻结提交", payload.expected_commit],
  ["准确性比较", "200 人；Oracle / Proposed / Simple / PerAgent；4 场景 × 30 Seeds。"],
  ["性能比较", "2k / 10k / 20k；Proposed / Simple / PerAgent；每规模每方法 10 次。"],
  ["准确性 CI", `${payload.analysis.bootstrap_ci}; ${payload.analysis.bootstrap_resamples} resamples; seed ${payload.analysis.bootstrap_seed}.`],
  ["性能 CI", payload.analysis.performance_ci],
  ["阈值含义", "越过阈值代表必须解释和审查；它不是自动判定整个研究失败。"],
  ["关键负面发现", "普通 Routine 的 TaskActive 时间相位与 Oracle 大量错位；身份、目标、第一步行为、住房与承诺事件仍保持。"],
  ["内存口径", "进程 UsedPhysical 峰值；不能解释为 Proposed 数据结构自身占用。"],
  ["数据根目录", "D:/AILODFormal/Phase8"],
  ["分析输入", analysisPath],
  ["生成器", `${projectRoot}/Tools/Phase8/Build-Phase8FormalWorkbook.mjs`],
  ["阅读顺序", "Headline → Threshold Audit → TaskActive → Accuracy Stats / Seed Plots → Performance Stats → QC → Raw。"],
];
styleSection(readme, "A3:A15");
readme.getRange("A3:B15").format.wrapText = true;
readme.showGridLines = false;
setWidths(readme, [24, 112, 4, 4, 4, 4]);

const perfLookup = new Map(payload.performance.stats.map((row) => [`${row.population}|${row.method}`, row]));
const accuracyLookup = new Map(payload.accuracy.stats.map((row) => [`${row.scenario}|${row.metric}`, row]));

headline.getRange("A1:N1").values = [["Phase 8 正式结果总览", null, null, null, null, null, null, null, null, null, null, null, null, null]];
styleTitle(headline, "A1:N1");
headline.getRange("A3:F3").values = [["性能：总 AI CPU（整场累计，ms）", null, null, null, null, null]];
styleSection(headline, "A3:F3");
writeTable(headline, 3, 0,
  ["Population", "Proposed median", "PerAgent median", "Simple median", "Proposed paired speedup", "Proposed peak process MB"],
  [2000, 10000, 20000].map((population) => {
    const proposed = perfLookup.get(`${population}|Proposed`);
    return [
      population,
      proposed.total_median_ms,
      perfLookup.get(`${population}|PerAgent`).total_median_ms,
      perfLookup.get(`${population}|Simple`).total_median_ms,
      proposed.paired_speedup_median,
      proposed.memory_peak_median_mb,
    ];
  }),
);
headline.getRange("A9:D9").values = [["准确性：Behavior TVD（越低越好）", null, null, null]];
styleSection(headline, "A9:D9");
writeTable(headline, 9, 0, ["Scenario", "Proposed", "Simple", "PerAgent"],
  ["None", "HarvestCap", "StateImport", "RepairAid"].map((scenario) => {
    const row = accuracyLookup.get(`${scenario}|Behavior.TVD`);
    return [scenario, row.proposed_median, row.simple_median, row.per_agent_median];
  }));
headline.getRange("A16:D16").values = [["连续性：TaskActive 状态不一致率（越低越好）", null, null, null]];
styleSection(headline, "A16:D16");
writeTable(headline, 16, 0, ["Scenario", "Proposed", "Simple", "PerAgent"],
  ["None", "HarvestCap", "StateImport", "RepairAid"].map((scenario) => {
    const row = accuracyLookup.get(`${scenario}|Continuity.TaskActiveStatusMismatchRate`);
    return [scenario, row.proposed_median, row.simple_median, row.per_agent_median];
  }));
headline.getRange("A23:F23").values = [["结论与边界", null, null, null, null, null]];
styleSection(headline, "A23:F23");
headline.getRange("A24:F29").values = [
  ["性能", "2k 时 Proposed 的固定成本较高；10k 开始快于 PerAgent；20k 的配对中位加速约 3.94 倍。", null, null, null, null],
  ["准确性", "Proposed 的宏观轨迹、政策效应和行为分布更接近 Oracle，明显优于 Simple。", null, null, null, null],
  ["连续性", "ResidentID、HomeID、住房状态、当前目标和第一步行为保持；普通 Routine 的 Active 时间相位存在系统性偏差。", null, null, null, null],
  ["阈值", "TaskActive 在 4 个场景的 30/30 Seeds 越过审查线；TaskRemainingHours 在每个场景有 1/30 Seed 越线。", null, null, null, null],
  ["内存", "Proposed 的进程峰值高于 PerAgent，现有数据不支持内存节省结论。", null, null, null, null],
  ["适用范围", "结论限定于冻结的单机、Shipping、NullRHI 和当前场景配置。", null, null, null, null],
];
for (let row = 24; row <= 29; row += 1) headline.mergeCells(`B${row}:F${row}`);
headline.getRange("A24:F29").format.wrapText = true;
headline.getRange("B4:F20").format.numberFormat = "0.000000";
headline.showGridLines = false;
setWidths(headline, [38, 24, 24, 24, 27, 29, 4, 18, 18, 18, 18, 18, 18, 18]);

const speedChart = headline.charts.add("line", headline.getRange("A4:C7"));
speedChart.title = "总 AI CPU：Proposed 在 10k 后跨过 PerAgent";
speedChart.hasLegend = true;
speedChart.xAxis = { axisType: "textAxis" };
speedChart.yAxis = { numberFormatCode: "0" };
speedChart.setPosition("H3", "N13");
const tvdChart = headline.charts.add("bar", headline.getRange("A10:D14"));
tvdChart.title = "Behavior TVD：Proposed 接近 Oracle";
tvdChart.hasLegend = true;
tvdChart.yAxis = { numberFormatCode: "0.000" };
tvdChart.setPosition("H15", "N25");

const accuracyFields = [
  "scenario", "metric", "n", "proposed_median", "proposed_q1", "proposed_q3", "proposed_mean",
  "simple_median", "simple_q1", "simple_q3", "simple_mean", "per_agent_median", "per_agent_q1",
  "per_agent_q3", "per_agent_mean", "proposed_minus_simple_mean", "proposed_minus_simple_bootstrap_ci_low",
  "proposed_minus_simple_bootstrap_ci_high", "proposed_minus_per_agent_mean",
  "proposed_minus_per_agent_bootstrap_ci_low", "proposed_minus_per_agent_bootstrap_ci_high",
];
writeTable(accuracyStats, 0, 0, accuracyFields, payload.accuracy.stats.map((row) => orderedValues(row, accuracyFields)));
accuracyStats.freezePanes.freezeRows(1);
accuracyStats.showGridLines = false;
setWidths(accuracyStats, accuracyFields.map((field) => field === "metric" ? 48 : field === "scenario" ? 16 : 18));
accuracyStats.getRange(`C2:U${payload.accuracy.stats.length + 1}`).format.numberFormat = "0.000000";

const pairedFields = ["scenario", "seed", "metric", "proposed", "simple", "per_agent", "oracle", "proposed_minus_simple", "proposed_minus_per_agent"];
writeTable(pairedSeeds, 0, 0, pairedFields, payload.accuracy.paired_rows.map((row) => orderedValues(row, pairedFields)));
pairedSeeds.freezePanes.freezeRows(1);
pairedSeeds.showGridLines = false;
setWidths(pairedSeeds, [16, 12, 50, 18, 18, 18, 18, 22, 25]);
pairedSeeds.getRange(`B2:B${payload.accuracy.paired_rows.length + 1}`).format.numberFormat = "0";
pairedSeeds.getRange(`D2:I${payload.accuracy.paired_rows.length + 1}`).format.numberFormat = "0.000000";

seedPlots.getRange("A1:N1").values = [["StateImport：逐 Seed 散点（30 个配对 Seeds）", null, null, null, null, null, null, null, null, null, null, null, null, null]];
styleTitle(seedPlots, "A1:N1");
const selectedMetrics = [
  "Behavior.TVD",
  "Trajectory.MarketWoodAvailable",
  "Trajectory.WoodPrice",
  "Continuity.TaskActiveStatusMismatchRate",
];
let plotStartRow = 2;
for (let index = 0; index < selectedMetrics.length; index += 1) {
  const metric = selectedMetrics[index];
  const rows = payload.accuracy.scatter_rows
    .filter((row) => row.scenario === "StateImport" && row.metric === metric)
    .sort((a, b) => a.seed - b.seed)
    .map((row, rowIndex) => [rowIndex + 1, row.proposed, row.simple, row.per_agent, row.seed]);
  seedPlots.getRange(`A${plotStartRow}:D${plotStartRow}`).values = [[metric, null, null, null]];
  styleSection(seedPlots, `A${plotStartRow}:D${plotStartRow}`);
  writeTable(seedPlots, plotStartRow, 0, ["Seed index", "Proposed", "Simple", "PerAgent", "Original seed"], rows);
  const chart = seedPlots.charts.add("scatter", seedPlots.getRange(`A${plotStartRow + 1}:D${plotStartRow + 31}`));
  chart.title = `${metric}: per-seed values`;
  chart.hasLegend = true;
  chart.yAxis = { numberFormatCode: "0.000" };
  const top = 2 + index * 16;
  chart.setPosition(`F${top}`, `N${top + 13}`);
  plotStartRow += 34;
}
seedPlots.showGridLines = false;
seedPlots.freezePanes.freezeRows(1);
setWidths(seedPlots, [13, 17, 17, 17, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16]);
seedPlots.getRange(`B2:D${plotStartRow}`).format.numberFormat = "0.000000";

const thresholdFields = ["scenario", "metric", "threshold", "unit", "rule", "min", "median", "max", "seeds_exceeding", "seed_count", "status"];
const thresholdRows = payload.accuracy.threshold_audit.map((row) => [
  ...orderedValues(row, thresholdFields.slice(0, 10)),
  row.seeds_exceeding > 0 ? "REVIEW" : "PASS",
]);
writeTable(thresholds, 0, 0, thresholdFields, thresholdRows);
thresholds.freezePanes.freezeRows(1);
thresholds.showGridLines = false;
setWidths(thresholds, [16, 48, 14, 12, 34, 14, 14, 14, 18, 14, 14]);
thresholds.getRange(`K2:K${thresholdRows.length + 1}`).conditionalFormats.add("containsText", { text: "REVIEW", format: { fill: "#FFF2CC", font: { color: "#9C6500", bold: true } } });
thresholds.getRange(`K2:K${thresholdRows.length + 1}`).conditionalFormats.add("containsText", { text: "PASS", format: { fill: "#E2F0D9", font: { color: "#375623", bold: true } } });
thresholds.getRange(`C2:J${thresholdRows.length + 1}`).format.numberFormat = "0.000000";

taskActive.getRange("A1:L1").values = [["TaskActive 分解：保留原指标，并解释差异来自哪里", null, null, null, null, null, null, null, null, null, null, null]];
styleTitle(taskActive, "A1:L1");
taskActive.getRange("A3:L3").values = [["汇总", null, null, null, null, null, null, null, null, null, null, null]];
styleSection(taskActive, "A3:L3");
const taskSummaryFields = Object.keys(payload.task_active.summary[0]);
writeTable(taskActive, 3, 0, taskSummaryFields, payload.task_active.summary.map((row) => orderedValues(row, taskSummaryFields)));
taskActive.getRange("A10:L10").values = [["解释", null, null, null, null, null, null, null, null, null, null, null]];
styleSection(taskActive, "A10:L10");
taskActive.getRange("A11:L14").values = [
  ["方向", "全部不一致都是 Proposed=Active、Oracle=Inactive。", null, null, null, null, null, null, null, null, null, null],
  ["共同点", "同一批快照中的 CurrentGoal、FirstAction 和 HomeState 保持一致。", null, null, null, null, null, null, null, null, null, null],
  ["差异来源", "普通 RoutineLife/Routine 的持续时间或观察时点不同。", null, null, null, null, null, null, null, null, null, null],
  ["承诺事件", "RestoreHome 等承诺相关活动的 TaskActive 不一致为 0；这是辅助诊断，不替代原始 TaskActive 指标。", null, null, null, null, null, null, null, null, null, null],
];
for (let row = 11; row <= 14; row += 1) taskActive.mergeCells(`B${row}:L${row}`);
taskActive.getRange("A11:L14").format.wrapText = true;
taskActive.getRange("A17:L17").values = [["逐 Seed", null, null, null, null, null, null, null, null, null, null, null]];
styleSection(taskActive, "A17:L17");
const taskSeedFields = Object.keys(payload.task_active.by_seed[0]);
writeTable(taskActive, 17, 0, taskSeedFields, payload.task_active.by_seed.map((row) => orderedValues(row, taskSeedFields)));
taskActive.freezePanes.freezeRows(3);
taskActive.showGridLines = false;
setWidths(taskActive, taskSeedFields.map((field) => field === "scenario" ? 16 : 18));
taskActive.getRange(`B5:L${payload.task_active.by_seed.length + 18}`).format.numberFormat = "0.000000";

const perfFields = Object.keys(payload.performance.stats[0]);
writeTable(performanceStats, 0, 0, perfFields, payload.performance.stats.map((row) => orderedValues(row, perfFields)));
performanceStats.freezePanes.freezeRows(1);
performanceStats.showGridLines = false;
setWidths(performanceStats, perfFields.map((field) => field === "method" ? 15 : 20));
performanceStats.getRange(`C2:${columnName(perfFields.length - 1)}${payload.performance.stats.length + 1}`).format.numberFormat = "0.000000";

const perfRunFields = Object.keys(payload.performance.runs[0]);
writeTable(performanceRuns, 0, 0, perfRunFields, payload.performance.runs.map((row) => orderedValues(row, perfRunFields)));
performanceRuns.freezePanes.freezeRows(1);
performanceRuns.showGridLines = false;
setWidths(performanceRuns, perfRunFields.map((field) => field.includes("run_id") ? 42 : field === "method" ? 15 : 19));
performanceRuns.getRange(`B2:${columnName(perfRunFields.length - 1)}${payload.performance.runs.length + 1}`).format.numberFormat = "0.000000";

const orderFields = Object.keys(payload.performance.order_sensitivity[0]);
writeTable(performanceOrder, 0, 0, orderFields, payload.performance.order_sensitivity.map((row) => orderedValues(row, orderFields)));
performanceOrder.freezePanes.freezeRows(1);
performanceOrder.showGridLines = false;
setWidths(performanceOrder, orderFields.map((field) => field === "method" ? 16 : 24));
performanceOrder.getRange(`C2:${columnName(orderFields.length - 1)}${payload.performance.order_sensitivity.length + 1}`).format.numberFormat = "0.000000";

const qcFields = ["check", "observed", "expected", "status", "note"];
writeTable(qc, 0, 0, qcFields, payload.qc.rows.map((row) => orderedValues(row, qcFields)));
qc.freezePanes.freezeRows(1);
qc.showGridLines = false;
setWidths(qc, [58, 72, 72, 13, 86]);
qc.getRange(`D2:D${payload.qc.rows.length + 1}`).conditionalFormats.add("containsText", { text: "PASS", format: { fill: "#E2F0D9", font: { color: "#375623", bold: true } } });
qc.getRange(`D2:D${payload.qc.rows.length + 1}`).conditionalFormats.add("containsText", { text: "FAIL", format: { fill: "#FCE4D6", font: { color: "#9C0006", bold: true } } });

for (const [sheetName, csvPath] of rawSources) {
  const rawSheet = workbook.worksheets.add(sheetName);
  const imported = await Workbook.fromCSV(await fs.readFile(csvPath, "utf8"), { sheetName: "Imported" });
  const values = imported.worksheets.getItem("Imported").getUsedRange(true).values;
  rawSheet.getRange(rangeAddress(0, 0, values.length, values[0].length)).values = values;
  styleHeader(rawSheet, rangeAddress(0, 0, 1, values[0].length));
  rawSheet.freezePanes.freezeRows(1);
  rawSheet.showGridLines = false;
  setWidths(rawSheet, [16, 32, 42, 12, 14, 12, 16, 58, 20, 46, 42, 26]);
  rawSheet.getRange(`F2:F${values.length}`).format.numberFormat = "0";
  rawSheet.getRange(`I2:I${values.length}`).format.numberFormat = "0.000000";
}

await fs.mkdir(outputDir, { recursive: true });
await fs.mkdir(supportDir, { recursive: true });

const inspectHeadline = await workbook.inspect({
  kind: "table", range: "Headline!A1:F29", include: "values,formulas",
  tableMaxRows: 29, tableMaxCols: 6, maxChars: 18000,
});
await fs.writeFile(`${supportDir}/headline_inspect.ndjson`, inspectHeadline.ndjson, "utf8");
const inspectBootstrap = await workbook.inspect({
  kind: "table", range: "Accuracy Stats!A1:U10", include: "values,formulas",
  tableMaxRows: 10, tableMaxCols: 21, maxChars: 20000,
});
await fs.writeFile(`${supportDir}/accuracy_bootstrap_inspect.ndjson`, inspectBootstrap.ndjson, "utf8");
const errorScan = await workbook.inspect({
  kind: "match", searchTerm: "#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A",
  options: { useRegex: true, maxResults: 300 }, summary: "final formula error scan",
});
await fs.writeFile(`${supportDir}/formula_error_scan.ndjson`, errorScan.ndjson, "utf8");

const previews = [
  ["README", "A1:B15", 0.9], ["Headline", "A1:N29", 1.0], ["Accuracy Stats", "A1:U20", 0.7],
  ["Accuracy Paired Seeds", "A1:I25", 0.8], ["Seed Plots", "A1:N137", 0.75],
  ["Threshold Audit", "A1:K29", 0.8], ["TaskActive", "A1:L45", 0.8],
  ["Performance Stats", `A1:${columnName(perfFields.length - 1)}10`, 0.75],
  ["Performance Runs", `A1:${columnName(perfRunFields.length - 1)}25`, 0.75],
  ["Performance Order", `A1:${columnName(orderFields.length - 1)}10`, 0.85],
  ["QC", `A1:E${payload.qc.rows.length + 1}`, 0.8],
  ["Accuracy Raw", "A1:L25", 0.75], ["Perf 2k Raw", "A1:L25", 0.75],
  ["Perf 10k Raw", "A1:L25", 0.75], ["Perf 20k Raw", "A1:L25", 0.75],
];
for (const [sheetName, range, scale] of previews) {
  const preview = await workbook.render({ sheetName, range, scale, format: "png" });
  await fs.writeFile(`${supportDir}/preview_${sheetName.replace(/\s+/g, "_").toLowerCase()}.png`, new Uint8Array(await preview.arrayBuffer()));
}

const output = await SpreadsheetFile.exportXlsx(workbook);
await output.save(outputPath);
process.stdout.write(JSON.stringify({
  outputPath,
  outputSha256: sha256(await fs.readFile(outputPath)),
  sheets: previews.map(([sheetName]) => sheetName),
  qcFailures: payload.qc.rows.filter((row) => row.status !== "PASS").length,
}));
