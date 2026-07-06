function parseCsv(text) {
  const lines = text.split(/\r?\n/).filter((line) => line.length > 0);
  if (lines.length < 2) {
    return { headers: [], rows: [] };
  }

  const headers = lines[0].split(",");
  const rows = lines.slice(1).map((line) => {
    const cells = line.split(",");
    const row = {};
    headers.forEach((h, idx) => {
      row[h] = cells[idx] || "";
    });
    return row;
  });

  return { headers, rows };
}

function discoverPeerIds(headers) {
  const ids = [];
  headers.forEach((h) => {
    if (h.endsWith("_online")) {
      ids.push(h.slice(0, -7));
    }
  });
  return ids;
}

function buildPeerSeries(rows, peerId, visibility) {
  if (!rows || rows.length === 0) {
    return { series: [], hasGridIE: false };
  }

  const vis = visibility || {};
  const showSolar = !!vis.showSolar;
  const showGridIE = !!vis.showGridIE;
  const showShaperInputs = !!vis.showShaperInputs;
  const showSmoothedAvailable = showSolar || showGridIE;

  const metrics = [
    ["actual_charge_w", "Actual Charge (W)", "line", "#f57c00"],
    ["charge_available_w", "Charge Available (W)", "line", "#1976d2"],
    ...(showSmoothedAvailable
      ? [["divert_smoothed_available_w", "Smoothed Available (W)", "line", "#009688"]]
      : []),
    ...(showSolar ? [["solar_w", "Solar (W)", "line", "#7b1fa2"]] : []),
    ...(showGridIE ? [["grid_ie_w", "Grid I/E (W)", "line", "#455a64"]] : []),
    ...(showShaperInputs ? [["live_pwr_w", "Live Power (W)", "line", "#c2185b"]] : []),
    ...(showShaperInputs
      ? [["shaper_smoothed_live_w", "Smoothed Live (W)", "line", "#6a1b9a"]]
      : []),
    ...(showShaperInputs ? [["shaper_max_w", "Shaper Max (W)", "line", "#0097a7"]] : []),
  ];

  const series = [];
  metrics.forEach(([suffix, label, type, color]) => {
    let key = `${peerId}_${suffix}`;
    // Backward compatibility: older CSV outputs only had pilot_w.
    if (suffix === "charge_available_w" && !(key in rows[0])) {
      const fallbackKey = `${peerId}_pilot_w`;
      if (fallbackKey in rows[0]) {
        key = fallbackKey;
      }
    }

    if (!(key in rows[0])) {
      return;
    }

    const points = rows
      .map((r) => ({
        x: new Date(r.time),
        y: parseFloat(r[key] || "0"),
      }))
      .filter((p) => Number.isFinite(p.y) && !Number.isNaN(p.x.getTime()));

    if (points.length === 0) {
      return;
    }

    const cfg = {
      name: label,
      type,
      lineThickness: 1.5,
      showInLegend: true,
      color,
      dataPoints: points,
    };

    if (suffix === "charge_available_w") {
      cfg.type = "area";
      cfg.fillOpacity = 0.25;
      cfg.lineThickness = 0.8;
    }

    series.push(cfg);
  });

  series.sort((a, b) => {
    const aIsFill = a.name === "Charge Available (W)";
    const bIsFill = b.name === "Charge Available (W)";
    return aIsFill === bIsFill ? 0 : (aIsFill ? -1 : 1);
  });

  return { series, hasGridIE: showGridIE };
}

async function loadScenarioSource(sourcePath) {
  if (!sourcePath) {
    return null;
  }
  const response = await fetch(sourcePath, { cache: "no-store" });
  if (!response.ok) {
    return null;
  }
  return response.json();
}

function buildPeerVisibilityFromScenario(scenarioSource, peerId) {
  const peers = (scenarioSource && Array.isArray(scenarioSource.peers))
    ? scenarioSource.peers
    : [];
  const peer = peers.find((p) => p && p.id === peerId);
  const inputs = (peer && peer.inputs) ? peer.inputs : {};
  const category = (scenarioSource && scenarioSource.meta && scenarioSource.meta.category)
    ? String(scenarioSource.meta.category)
    : "";

  const hasSolarInput = !!inputs.solar;
  const hasGridIEInput = !!inputs.grid_ie;
  const hasShaperInput = !!inputs.live_pwr;

  return {
    showSolar: hasSolarInput && !hasGridIEInput,
    showGridIE: hasGridIEInput,
    showShaperInputs: hasShaperInput,
  };
}

async function loadCsvRows(csvPath) {
  const response = await fetch(csvPath, { cache: "no-store" });
  const text = await response.text();
  const parsed = parseCsv(text);
  return parsed;
}

function createChart(containerId, title, series, options) {
  const opts = options || {};
  const normalizedSeries = (series || [])
    .filter((s) => !!s && Array.isArray(s.dataPoints) && s.dataPoints.length > 0)
    .map((s) => ({
      ...s,
      dataPoints: s.dataPoints.filter(
        (p) =>
          !!p &&
          p.x instanceof Date &&
          !Number.isNaN(p.x.getTime()) &&
          Number.isFinite(p.y)
      ),
    }))
    .filter((s) => s.dataPoints.length > 0);

  if (normalizedSeries.length === 0) {
    const el = document.getElementById(containerId);
    if (el) {
      el.textContent = "No numeric data available for this peer in the selected scenario.";
      el.style.display = "flex";
      el.style.alignItems = "center";
      el.style.justifyContent = "center";
      el.style.color = "#666";
      el.style.border = "1px dashed #ccc";
      el.style.padding = "8px";
      el.style.boxSizing = "border-box";
    }
    return;
  }

  try {
    const chart = new CanvasJS.Chart(containerId, {
      animationEnabled: false,
      zoomEnabled: true,
      title: { text: title, fontSize: 18 },
      legend: { fontSize: 12 },
      axisX: { valueFormatString: "HH:mm:ss" },
      axisY: { title: "Power (W)", ...(opts.hasGridIE ? {} : { minimum: 0 }) },
      // Show all visible series values for the hovered timestamp.
      toolTip: { shared: true },
      data: normalizedSeries,
    });
    chart.render();
  } catch (err) {
    const el = document.getElementById(containerId);
    if (el) {
      el.textContent = `Chart render failed: ${String(err)}`;
      el.style.display = "flex";
      el.style.alignItems = "center";
      el.style.justifyContent = "center";
      el.style.color = "#a94442";
      el.style.border = "1px dashed #a94442";
      el.style.padding = "8px";
      el.style.boxSizing = "border-box";
    }
  }
}

async function renderScenario(container, scenario) {
  const scenarioBlock = document.createElement("section");
  scenarioBlock.className = "scenario-block";

  const h3 = document.createElement("h3");
  h3.textContent = scenario.title || scenario.id || "Scenario";
  scenarioBlock.appendChild(h3);
  // CanvasJS requires the target container element to exist in the live DOM
  // before Chart(...) is constructed.
  container.appendChild(scenarioBlock);

  const [parsed, scenarioSource] = await Promise.all([
    loadCsvRows(scenario.csv),
    loadScenarioSource(scenario.source),
  ]);
  if (!parsed.rows || parsed.rows.length === 0) {
    const empty = document.createElement("p");
    empty.textContent = "No rows found in CSV output for this scenario.";
    scenarioBlock.appendChild(empty);
    return;
  }

  const peerIds = discoverPeerIds(parsed.headers);
  if (peerIds.length === 0) {
    const empty = document.createElement("p");
    empty.textContent = "No peer columns discovered in CSV header.";
    scenarioBlock.appendChild(empty);
    return;
  }

  peerIds.forEach((peerId, idx) => {
    const peerTitle = document.createElement("h4");
    peerTitle.textContent = `${peerId}`;
    scenarioBlock.appendChild(peerTitle);

    const chartDiv = document.createElement("div");
    chartDiv.id = `${scenario.id}_${scenario.profile}_${peerId}_${idx}`;
    chartDiv.style.width = "100%";
    chartDiv.style.height = "280px";
    scenarioBlock.appendChild(chartDiv);

    const visibility = buildPeerVisibilityFromScenario(scenarioSource, peerId);
    const result = buildPeerSeries(parsed.rows, peerId, visibility);
    createChart(chartDiv.id, `${scenario.title} - ${peerId}`, result.series, { hasGridIE: result.hasGridIE });
  });

}

async function fetchIndex(indexPath = "output/index.json") {
  const response = await fetch(indexPath, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`Failed to load ${indexPath}: ${response.status}`);
  }
  return response.json();
}

async function fetchProfiles(indexPath = "output/index.json") {
  const index = await fetchIndex(indexPath);
  const set = new Set((index.scenarios || []).map((s) => s.profile).filter(Boolean));
  return Array.from(set).sort();
}

async function fetchProfilesForCategory(category, indexPath = "output/index.json") {
  const index = await fetchIndex(indexPath);
  const set = new Set(
    (index.scenarios || [])
      .filter((s) => s.category === category)
      .map((s) => s.profile)
      .filter(Boolean)
  );
  return Array.from(set).sort();
}

async function fetchCategories(indexPath = "output/index.json") {
  const index = await fetchIndex(indexPath);
  const set = new Set((index.scenarios || []).map((s) => s.category).filter(Boolean));
  return Array.from(set).sort();
}

function groupByCategory(scenarios) {
  const grouped = {};
  scenarios.forEach((s) => {
    if (!grouped[s.category]) {
      grouped[s.category] = [];
    }
    grouped[s.category].push(s);
  });
  return grouped;
}

async function renderIndex(targetId, profile, indexPath = "output/index.json") {
  const root = document.getElementById(targetId);
  root.innerHTML = "";

  const index = await fetchIndex(indexPath);
  const scenarios = index.scenarios || [];
  let filtered = scenarios.filter((s) => s.profile === profile);
  if (filtered.length === 0) {
    filtered = scenarios;
  }

  if (filtered.length === 0) {
    const empty = document.createElement("p");
    empty.textContent = "No scenarios available. Run run_simulations.py or use interactive run first.";
    root.appendChild(empty);
    return;
  }

  const grouped = groupByCategory(filtered);

  for (const category of Object.keys(grouped).sort()) {
    const section = document.createElement("section");
    const title = document.createElement("h2");
    title.textContent = category;
    section.appendChild(title);
    root.appendChild(section);

    for (const scenario of grouped[category]) {
      await renderScenario(section, scenario);
    }
  }
}

async function renderCategory(targetId, category, profiles, indexPath = "output/index.json") {
  const root = document.getElementById(targetId);
  root.innerHTML = "";

  const index = await fetchIndex(indexPath);
  const scenarios = (index.scenarios || []).filter((s) => s.category === category);

  const selectedProfiles = Array.isArray(profiles)
    ? profiles.filter(Boolean)
    : (profiles ? [profiles] : []);

  const filtered = selectedProfiles.length > 0
    ? scenarios.filter((s) => selectedProfiles.includes(s.profile))
    : scenarios;

  if (filtered.length === 0) {
    const empty = document.createElement("p");
    empty.textContent = "No scenarios available for the selected profiles.";
    root.appendChild(empty);
    return;
  }

  for (const scenario of filtered) {
    await renderScenario(root, scenario);
  }
}

async function runInteractiveSimulation(payload) {
  const response = await fetch("/simulation", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload || {}),
  });
  if (!response.ok) {
    throw new Error(`Simulation request failed: ${response.status}`);
  }
}
