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

let activeRenderToken = 0;

function isRenderTokenCurrent(token) {
  return token === activeRenderToken;
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
  const showLoadshareAllocated = !!vis.showLoadshareAllocated;
  const showSmoothedAvailable = showSolar || showGridIE;

  const metrics = [
    ["actual_charge_w", "Actual Charge (W)", "line", "#f57c00"],
    ["charge_available_w", "Charge Available (W)", "line", "#1976d2"],
    ...(showSmoothedAvailable
      ? [["divert_smoothed_available_w", "Smoothed Available (W)", "line", "#009688"]]
      : []),
    ...(showLoadshareAllocated
      ? [["loadshare_allocated_w", "Loadshare Allocated (W)", "line", "#388e3c"]]
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

function getEvseStateVisual(stateCode) {
  const raw = String(stateCode || "").trim().toLowerCase();
  let code = Number.NaN;
  if (raw.startsWith("0x")) {
    code = Number.parseInt(raw.slice(2), 16);
  } else if (/^\d+$/.test(raw)) {
    code = Number.parseInt(raw, 10);
  } else {
    const names = {
      ready: 1,
      connected: 2,
      charging: 3,
      sleeping: 254,
      disabled: 255,
    };
    code = names[raw];
  }
  // Match EVSE LED semantics while keeping sleeping visibly distinct from charging.
  const map = {
    1: { label: "No EV connected", state: "ready", color: "#9e9e9e" },
    2: { label: "Connected", state: "connected", color: "#03a9f4" },
    3: { label: "Charging", state: "charging", color: "#00ffff" },
    4: { label: "Vent Required", state: "vent", color: "#9c27b0" },
    5: { label: "Diode Fault", state: "diode_fault", color: "#f44336" },
    6: { label: "GFCI Fault", state: "gfci_fault", color: "#f44336" },
    7: { label: "No Ground", state: "no_ground", color: "#f44336" },
    8: { label: "Stuck Relay", state: "stuck_relay", color: "#f44336" },
    9: { label: "GFCI Self-test Fault", state: "gfci_self_test_fault", color: "#f44336" },
    10: { label: "Over Temperature", state: "over_temp", color: "#ff9800" },
    254: { label: "Sleeping", state: "sleeping", color: "#2dd9e8" },
    255: { label: "Disabled", state: "disabled", color: "#607d8b" },
  };

  return map[code] || { label: `State ${code}`, state: `state_${code}`, color: "#78909c" };
}

function getClaimStateVisual(claimState) {
  const state = String(claimState || "none").trim().toLowerCase();
  const map = {
    none: { label: "No claim", state: "none", color: "#9e9e9e" },
    active: { label: "Active claim", state: "active", color: "#43a047" },
    disabled: { label: "Disable claim", state: "disabled", color: "#e53935" },
    other: { label: "Other claim", state: "other", color: "#fdd835" },
  };

  return map[state] || { label: `${state || "other"} claim`, state: "other", color: "#fdd835" };
}

function formatClaimOwnerLabel(owner) {
  return String(owner || "Claims")
    .split("_")
    .filter(Boolean)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join(" ");
}

function parseClaimDetails(row, peerId) {
  const details = String(row[`${peerId}_claim_details`] || "").trim();
  const claimsByOwner = {};

  if (!details || details === "No active claims" || details === "No claim details") {
    return { claimsByOwner, details };
  }

  const claimsPart = details.split(" ; wins ")[0] || "";
  claimsPart
    .split(" | ")
    .map((chunk) => chunk.trim())
    .filter(Boolean)
    .forEach((chunk) => {
      const match = chunk.match(/^([a-z0-9_]+)@\d+:([a-z_]+)/i);
      if (match) {
        claimsByOwner[match[1].toLowerCase()] = {
          state: match[2].toLowerCase(),
          summary: chunk,
        };
      }
    });

  return { claimsByOwner, details };
}

function buildTimelineSegments(rows, getValue, getVisual) {
  const points = rows
    .map((row) => ({ row, ms: Date.parse(row.time) }))
    .filter((p) => Number.isFinite(p.ms));

  if (points.length === 0) {
    return [];
  }

  const segments = [];
  for (let i = 0; i < points.length; i += 1) {
    const currentValue = getValue(points[i].row);
    const startMs = points[i].ms;
    let endMs = points[i].ms + 1;

    let j = i + 1;
    while (j < points.length) {
      const nextValue = getValue(points[j].row);
      if (nextValue !== currentValue) {
        break;
      }
      endMs = points[j].ms;
      j += 1;
    }

    if (j < points.length) {
      endMs = points[j].ms;
    } else if (i > 0) {
      const delta = Math.max(1, points[i].ms - points[i - 1].ms);
      endMs = points[i].ms + delta;
    }

    const visual = getVisual(currentValue, points[i].row);
    segments.push({
      startMs,
      endMs: Math.max(endMs, startMs + 1),
      color: visual.color,
      title: visual.title,
      stateClass: visual.stateClass,
    });

    i = j - 1;
  }

  return segments;
}

function renderTimelineRow(label, segments) {
  const row = document.createElement("div");
  row.className = "timeline-row";

  const labelEl = document.createElement("div");
  labelEl.className = "timeline-label";
  labelEl.textContent = label;
  row.appendChild(labelEl);

  const track = document.createElement("div");
  track.className = "timeline-track";

  segments.forEach((segment, idx) => {
    const block = document.createElement("span");
    block.className = `timeline-segment ${segment.stateClass || ""}`.trim();
    block.dataset.startMs = String(segment.startMs);
    block.dataset.endMs = String(segment.endMs);
    if (idx === segments.length - 1) {
      block.dataset.lastSegment = "true";
    }
    block.title = segment.title || "";
    block.style.backgroundColor = segment.color;
    block.style.left = "0%";
    block.style.width = "0%";
    track.appendChild(block);
  });

  row.appendChild(track);
  return row;
}

function createPeerTimeline(rows, peerId, options) {
  const opts = options || {};
  const stateKey = `${peerId}_state`;
  const claimStateKey = `${peerId}_claim_state`;
  const loadshareAllocatedKey = `${peerId}_loadshare_allocated_w`;
  const hasExplicitClaims = rows && rows.length > 0 && claimStateKey in rows[0];
  const hasDerivedLoadSharingClaims = rows && rows.length > 0 && opts.fallbackLoadSharingClaims && loadshareAllocatedKey in rows[0];
  if (!rows || rows.length === 0 || (!(stateKey in rows[0]) && !hasExplicitClaims && !hasDerivedLoadSharingClaims)) {
    return null;
  }

  const wrapper = document.createElement("div");
  wrapper.className = "peer-timeline";

  if (stateKey in rows[0]) {
    const stateSegments = buildTimelineSegments(
      rows,
      (row) => row[stateKey],
      (value) => {
        const visual = getEvseStateVisual(value);
        return {
          color: visual.color,
          title: `${visual.label} (${visual.state})`,
          stateClass: `state-${visual.state}`,
        };
      }
    );

    wrapper.appendChild(renderTimelineRow("EVSE State", stateSegments));
  }

  if (hasExplicitClaims) {
    const owners = new Set();
    rows.forEach((row) => {
      Object.keys(parseClaimDetails(row, peerId).claimsByOwner).forEach((owner) => owners.add(owner));
    });

    const ownerList = Array.from(owners);
    if (ownerList.length === 0) {
      ownerList.push("claims");
    }

    ownerList.forEach((owner) => {
      const claimSegments = buildTimelineSegments(
        rows,
        (row) => {
          const parsed = parseClaimDetails(row, peerId);
          const ownerClaim = parsed.claimsByOwner[owner];
          return `${owner}:${ownerClaim ? ownerClaim.state : String(row[claimStateKey] || "none")}:${parsed.details}`;
        },
        (_value, row) => {
          const parsed = parseClaimDetails(row, peerId);
          const ownerClaim = parsed.claimsByOwner[owner];
          const claimState = ownerClaim ? ownerClaim.state : String(row[claimStateKey] || "none");
          const visual = getClaimStateVisual(claimState);
          const details = parsed.details || "No claim details";
          return {
            color: visual.color,
            title: `${formatClaimOwnerLabel(owner)} (${visual.label})\n${details}`,
            stateClass: `claim-${visual.state}`,
          };
        }
      );

      wrapper.appendChild(renderTimelineRow(formatClaimOwnerLabel(owner), claimSegments));
    });
  } else if (hasDerivedLoadSharingClaims) {
    const claimSegments = buildTimelineSegments(
      rows,
      (row) => {
        const allocatedW = Number.parseFloat(row[loadshareAllocatedKey] || "0");
        return Number.isFinite(allocatedW) && allocatedW > 0 ? "active" : "none";
      },
      (value, row) => {
        const visual = getClaimStateVisual(value);
        const allocatedW = Number.parseFloat(row[loadshareAllocatedKey] || "0");
        const detail = Number.isFinite(allocatedW)
          ? `Derived from loadshare_allocated_w=${allocatedW.toFixed(1)} W`
          : "Derived from loadshare_allocated_w";
        return {
          color: visual.color,
          title: `Load Sharing (${visual.label})\n${detail}`,
          stateClass: `claim-${visual.state}`,
        };
      }
    );

    wrapper.appendChild(renderTimelineRow("Load Sharing", claimSegments));
  }

  return wrapper;
}

function layoutTimelineSegments(peerTimeline, domainMin, domainMax, dataMax) {
  if (!peerTimeline || !Number.isFinite(domainMin) || !Number.isFinite(domainMax)) {
    return;
  }

  const range = Math.max(1, domainMax - domainMin);
  const lastSegmentEnd = Number.isFinite(dataMax) ? Math.min(dataMax, domainMax) : domainMax;
  const segments = peerTimeline.querySelectorAll(".timeline-segment");
  segments.forEach((segment) => {
    const startMs = Number.parseFloat(segment.dataset.startMs || "NaN");
    const rawEndMs = Number.parseFloat(segment.dataset.endMs || "NaN");
    const endMs = segment.dataset.lastSegment === "true"
      ? lastSegmentEnd
      : rawEndMs;
    if (!Number.isFinite(startMs) || !Number.isFinite(endMs)) {
      segment.style.display = "none";
      return;
    }

    const leftPct = ((startMs - domainMin) / range) * 100;
    const rightPct = ((endMs - domainMin) / range) * 100;
    const clippedLeft = Math.max(0, Math.min(100, leftPct));
    const clippedRight = Math.max(0, Math.min(100, rightPct));
    const widthPct = clippedRight - clippedLeft;

    if (widthPct <= 0) {
      segment.style.display = "none";
      return;
    }

    segment.style.display = "block";
    segment.style.left = `${clippedLeft}%`;
    segment.style.width = `${Math.max(0.2, widthPct)}%`;
  });
}

function alignTimelineToChart(peerTimeline, chart) {
  if (!peerTimeline || !chart || !chart.axisX || !chart.axisX[0]) {
    return;
  }

  const axis = chart.axisX[0];
  const getOpt = (name) => (typeof axis.get === "function" ? axis.get(name) : undefined);

  const viewportMin = getOpt("viewportMinimum");
  const viewportMax = getOpt("viewportMaximum");
  const minimum = getOpt("minimum");
  const maximum = getOpt("maximum");

  const segments = peerTimeline.querySelectorAll(".timeline-segment");
  let minMs = Number.POSITIVE_INFINITY;
  let maxMs = Number.NEGATIVE_INFINITY;
  segments.forEach((seg) => {
    const startMs = Number.parseFloat(seg.dataset.startMs || "NaN");
    const endMs = Number.parseFloat(seg.dataset.endMs || "NaN");
    if (Number.isFinite(startMs)) {
      minMs = Math.min(minMs, startMs);
    }
    if (Number.isFinite(endMs)) {
      maxMs = Math.max(maxMs, endMs);
    }
  });

  if (!Number.isFinite(minMs) || !Number.isFinite(maxMs) || maxMs <= minMs) {
    return;
  }

  const axisMin = Number.isFinite(viewportMin) ? viewportMin : (Number.isFinite(minimum) ? minimum : minMs);
  const axisMax = Number.isFinite(viewportMax) ? viewportMax : (Number.isFinite(maximum) ? maximum : maxMs);

  let dataMax = Number.NEGATIVE_INFINITY;
  (chart.options && Array.isArray(chart.options.data) ? chart.options.data : []).forEach((series) => {
    (Array.isArray(series.dataPoints) ? series.dataPoints : []).forEach((point) => {
      const pointMs = point && point.x instanceof Date ? point.x.getTime() : Number.NaN;
      if (Number.isFinite(pointMs)) {
        dataMax = Math.max(dataMax, pointMs);
      }
    });
  });

  const timelineMax = Number.isFinite(dataMax) && dataMax > axisMin
    ? Math.min(axisMax, dataMax)
    : axisMax;

  layoutTimelineSegments(peerTimeline, axisMin, timelineMax, dataMax);

  const plotX1 = chart.plotArea && Number.isFinite(chart.plotArea.x1) ? chart.plotArea.x1 : Number.NaN;
  const plotX2 = chart.plotArea && Number.isFinite(chart.plotArea.x2) ? chart.plotArea.x2 : Number.NaN;
  const chartWidth = chart.container ? chart.container.clientWidth : 0;
  if (!Number.isFinite(plotX1) || !Number.isFinite(plotX2) || chartWidth <= 0) {
    return;
  }

  const leftInset = Math.max(0, plotX1);
  let trackRight = plotX2;
  if (Number.isFinite(timelineMax) && timelineMax < axisMax) {
    try {
      const timelineMaxPx = axis.convertValueToPixel(timelineMax);
      if (Number.isFinite(timelineMaxPx)) {
        trackRight = timelineMaxPx;
      }
    } catch (_) {
      trackRight = plotX2;
    }
  }
  const trackWidth = Math.max(0, trackRight - plotX1);

  const rows = peerTimeline.querySelectorAll(".timeline-row");
  rows.forEach((row) => {
    row.style.setProperty("--timeline-left-inset", `${leftInset}px`);
    row.style.setProperty("--timeline-track-width", `${trackWidth}px`);
    const track = row.querySelector(".timeline-track");
    if (track) {
      track.style.marginLeft = "0";
      track.style.marginRight = "0";
    }
  });
}

function findNearestDataPointX(seriesList, targetXValue) {
  if (!Array.isArray(seriesList) || seriesList.length === 0 || !Number.isFinite(targetXValue)) {
    return null;
  }

  let nearestX = null;
  let nearestDistance = Number.POSITIVE_INFINITY;

  seriesList.forEach((series) => {
    if (!series || !Array.isArray(series.dataPoints)) {
      return;
    }
    series.dataPoints.forEach((point) => {
      if (!point || !(point.x instanceof Date)) {
        return;
      }
      const x = point.x.getTime();
      const distance = Math.abs(x - targetXValue);
      if (distance < nearestDistance) {
        nearestDistance = distance;
        nearestX = x;
      }
    });
  });

  return nearestX;
}

function ensureHoverLine(chart) {
  if (!chart || !chart.container) {
    return null;
  }

  const host = chart.container;
  if (!host.style.position) {
    host.style.position = "relative";
  }

  let line = host.querySelector(".sync-hover-line");
  if (!line) {
    line = document.createElement("div");
    line.className = "sync-hover-line";
    line.style.position = "absolute";
    line.style.top = "0";
    line.style.bottom = "0";
    line.style.width = "0";
    line.style.marginLeft = "0";
    line.style.borderLeft = "2px dotted #5c6b7a";
    line.style.opacity = "0.9";
    line.style.pointerEvents = "none";
    line.style.zIndex = "1000";
    line.style.display = "none";
    host.appendChild(line);
  }

  return line;
}

function attachScenarioHoverSync(charts) {
  const activeCharts = (charts || []).filter((c) => !!c);
  if (activeCharts.length < 2) {
    return;
  }

  const syncAll = (sourceChart, targetXValue) => {
    const nearestX = findNearestDataPointX(sourceChart.options.data, targetXValue);
    if (!Number.isFinite(nearestX)) {
      return;
    }

    activeCharts.forEach((chart) => {
      try {
        if (chart.toolTip && typeof chart.toolTip.showAtX === "function") {
          chart.toolTip.showAtX(nearestX);
        }

        const line = ensureHoverLine(chart);
        if (line && chart.axisX && chart.axisX[0]) {
          const pixelX = chart.axisX[0].convertValueToPixel(nearestX);
          if (Number.isFinite(pixelX)) {
            line.style.left = `${pixelX}px`;
            line.style.display = "block";
          }
        }
      } catch (_) {
        // Ignore sync errors per-chart so one failure does not break others.
      }
    });
  };

  const clearAll = () => {
    activeCharts.forEach((chart) => {
      try {
        if (chart.toolTip && typeof chart.toolTip.hide === "function") {
          chart.toolTip.hide();
        }
        const line = ensureHoverLine(chart);
        if (line) {
          line.style.display = "none";
        }
      } catch (_) {
        // Ignore cleanup errors per-chart.
      }
    });
  };

  activeCharts.forEach((chart) => {
    const canvas = chart.container
      ? chart.container.querySelector("canvas.canvasjs-chart-canvas:last-of-type")
      : null;
    if (!canvas) {
      return;
    }

    canvas.addEventListener("mousemove", (event) => {
      const rect = canvas.getBoundingClientRect();
      const pixelX = event.clientX - rect.left;
      if (!Number.isFinite(pixelX)) {
        return;
      }
      let xValue;
      try {
        xValue = chart.axisX[0].convertPixelToValue(pixelX);
      } catch (_) {
        return;
      }
      syncAll(chart, xValue);
    });

    canvas.addEventListener("mouseleave", clearAll);
  });
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
  const hasLoadsharing = category === "loadsharing";

  return {
    showSolar: hasSolarInput && !hasGridIEInput,
    showGridIE: hasGridIEInput,
    showShaperInputs: hasShaperInput,
    showLoadshareAllocated: hasLoadsharing,
  };
}

async function loadCsvRows(csvPath) {
  const response = await fetch(csvPath, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`Failed to load ${csvPath}: ${response.status} ${response.statusText}`);
  }
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
    let chart = null;
    const onRangeChanged = (e) => {
      if (typeof opts.onRangeChanged === "function") {
        opts.onRangeChanged((e && e.chart) ? e.chart : chart);
      }
    };

    chart = new CanvasJS.Chart(containerId, {
      animationEnabled: false,
      zoomEnabled: true,
      title: { text: title, fontSize: 18 },
      legend: { fontSize: 12 },
      axisX: { valueFormatString: "HH:mm:ss" },
      axisY: { title: "Power (W)", ...(opts.hasGridIE ? {} : { minimum: 0 }) },
      rangeChanged: onRangeChanged,
      // Show all visible series values for the hovered timestamp.
      toolTip: { shared: true },
      data: normalizedSeries,
    });
    chart.render();
    window.__simCharts = window.__simCharts || {};
    window.__simCharts[containerId] = chart;
    if (typeof opts.onRangeChanged === "function") {
      opts.onRangeChanged(chart);
    }
    return chart;
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
    return null;
  }
}

async function renderScenario(container, scenario, renderToken) {
  if (!isRenderTokenCurrent(renderToken)) {
    return;
  }

  const scenarioBlock = document.createElement("section");
  scenarioBlock.className = "scenario-block";

  const h3 = document.createElement("h3");
  h3.textContent = scenario.title || scenario.id || "Scenario";
  scenarioBlock.appendChild(h3);

  if (!isRenderTokenCurrent(renderToken)) {
    return;
  }

  // CanvasJS requires the target container element to exist in the live DOM
  // before Chart(...) is constructed.
  container.appendChild(scenarioBlock);

  let parsed;
  let scenarioSource;
  try {
    [parsed, scenarioSource] = await Promise.all([
      loadCsvRows(scenario.csv),
      loadScenarioSource(scenario.source),
    ]);
  } catch (err) {
    if (!isRenderTokenCurrent(renderToken)) {
      return;
    }
    const error = document.createElement("p");
    error.textContent = `Failed to load scenario data: ${String(err)}`;
    error.style.color = "#a94442";
    scenarioBlock.appendChild(error);
    return;
  }

  if (!isRenderTokenCurrent(renderToken)) {
    return;
  }

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

  const charts = [];
  peerIds.forEach((peerId, idx) => {
    const peerTitle = document.createElement("h4");
    peerTitle.textContent = `${peerId}`;
    scenarioBlock.appendChild(peerTitle);

    const peerTimeline = createPeerTimeline(parsed.rows, peerId, {
      fallbackLoadSharingClaims: scenario.category === "loadsharing",
    });
    if (peerTimeline) {
      scenarioBlock.appendChild(peerTimeline);
    }

    const chartDiv = document.createElement("div");
    chartDiv.id = `${scenario.id}_${scenario.profile}_${peerId}_${idx}`;
    chartDiv.style.width = "100%";
    chartDiv.style.height = "280px";
    scenarioBlock.appendChild(chartDiv);

    const visibility = buildPeerVisibilityFromScenario(scenarioSource, peerId);
    const result = buildPeerSeries(parsed.rows, peerId, visibility);
    const chart = createChart(chartDiv.id, `${scenario.title} - ${peerId}`, result.series, {
      hasGridIE: result.hasGridIE,
      onRangeChanged: (activeChart) => {
        if (peerTimeline && activeChart) {
          alignTimelineToChart(peerTimeline, activeChart);
        }
      },
    });
    charts.push(chart);
  });

  attachScenarioHoverSync(charts);

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

  const renderToken = ++activeRenderToken;

  const index = await fetchIndex(indexPath);
  if (!isRenderTokenCurrent(renderToken)) {
    return;
  }
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
    if (!isRenderTokenCurrent(renderToken)) {
      return;
    }
    await renderScenario(root, scenario, renderToken);
  }
}

async function runInteractiveSimulation(overrides) {
  await fetch("/simulation", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(overrides || {}),
  });
}
