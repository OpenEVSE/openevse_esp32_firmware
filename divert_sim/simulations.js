
var divert_datasets = [
  { id: "day1", class: "solar", title: "Day 1 (Solar)" },
  { id: "day2", class: "solar", title: "Day 2 (Solar)" },
  { id: "day3", class: "solar", title: "Day 3 (Solar)" },
  { id: "almostperfect", class: "solar", title: "Almost Perfect" },
  { id: "CloudyMorning", class: "solar", title: "Cloudy Morning" },
  { id: "solar-vrms", class: "solar", title: "Solar with Voltage feed" },
  { id: "day1_grid_ie", class: "gridie", title: "Day 1 (Grid I+/E-)" },
  { id: "day2_grid_ie", class: "gridie", title: "Day 2 (Grid I+/E-)" },
  { id: "day3_grid_ie", class: "gridie", title: "Day 3 (Grid I+/E-)" },
  { id: "Energy_and_Power_Day_2020-03-22", class: "solar", title: "Energy and Power Day 2020-03-22" },
  { id: "Energy_and_Power_Day_2020-03-31", class: "solar", title: "Energy and Power Day 2020-03-31" },
  { id: "Energy_and_Power_Day_2020-04-01", class: "solar", title: "Energy and Power Day 2020-04-01" },
];

var shaper_datasets = [
  { id: "data_shaper", class: "shaper", title: "Shaper example 1" },
];

var loadsharing_datasets = [
  { id: "loadsharing_2peer_basic", class: "loadsharing", title: "Load Sharing: 2 Peer Basic" },
  { id: "loadsharing_3peer_staggered", class: "loadsharing", title: "Load Sharing: 3 Peer Staggered" },
  { id: "loadsharing_variable_supply", class: "loadsharing", title: "Load Sharing: Variable Supply" },
  { id: "loadsharing_ev_limited", class: "loadsharing", title: "Load Sharing: EV Limited" },
  { id: "loadsharing_peer_offline", class: "loadsharing", title: "Load Sharing: Peer Offline" },
  { id: "loadsharing_failsafe_disable", class: "loadsharing", title: "Load Sharing: Failsafe Disable" },
  { id: "loadsharing_insufficient", class: "loadsharing", title: "Load Sharing: Insufficient Supply" },
  { id: "loadsharing_ev_taper", class: "loadsharing", title: "Load Sharing: EV Taper" },
  { id: "loadsharing_longrun_2peer_handoff", class: "loadsharing", title: "Load Sharing: Long Run 2 Peer Handoff" },
];

var loadsharing_summary = {};

var summary = {};
function init_summary(divert_profiles, shaper_profiles) {
  for (const profile of divert_profiles) {
    summary[profile] = {};
    for (const dataset of divert_datasets) {
      summary[profile][dataset.id] = {};
    }
  }
  for (const profile of shaper_profiles) {
    summary[profile] = {};
    for (const dataset of shaper_datasets) {
      summary[profile][dataset.id] = {};
    }
  }
}

function getDataPointsFromCSV(csv) {
  var dataPoints = csvLines = points = [];
  csvLines = csv.split(/[\r?\n|\r|\n]+/);

  for (var i = 1; i < csvLines.length; i++)
    if (csvLines[i].length > 0) {
      points = csvLines[i].split(",");

      while (dataPoints.length < points.length - 1) {
        dataPoints.push([]);
      }

      var date = parseTimeValue(points[0]);
      for (var p = 1; p < points.length; p++) {
        dataPoints[p - 1].push({
          x: date,
          y: parseFloat(points[p])
        });
      }
    }
  return dataPoints;
}

function parseTimeValue(value) {
  var numeric = /^\d+(\.\d+)?$/.test(value.trim());
  if (numeric) {
    return new Date(parseFloat(value) * 1000);
  }

  var parsed = moment(value, "DD/MM/YYYY HH:mm:ss", true);
  if (parsed.isValid()) {
    return parsed.toDate();
  }

  return new Date(value);
}

function parseCsvWithHeader(csv) {
  var lines = csv.split(/[\r?\n|\r|\n]+/).filter(function (line) {
    return line.length > 0;
  });
  if (lines.length < 2) {
    return { headers: [], rows: [] };
  }

  var headers = lines[0].split(",");
  var rows = [];

  for (var i = 1; i < lines.length; i++) {
    var cols = lines[i].split(",");
    if (cols.length !== headers.length) {
      continue;
    }

    var row = {};
    for (var h = 0; h < headers.length; h++) {
      row[headers[h]] = cols[h];
    }
    rows.push(row);
  }

  return { headers: headers, rows: rows };
}

function getLoadSharingSeriesFromCSV(csv) {
  var parsed = parseCsvWithHeader(csv);
  var headers = parsed.headers;
  var rows = parsed.rows;

  var peerPowerColumns = headers.filter(function (name) {
    return name.endsWith("_actual_power_w");
  });

  var peerIds = peerPowerColumns.map(function (column) {
    return column.replace("_actual_power_w", "");
  });

  var series = [
    {
      name: "Available Power",
      type: "line",
      lineThickness: 2,
      showInLegend: true,
      dataPoints: [],
    },
    {
      name: "Total Demand",
      type: "line",
      lineThickness: 2,
      lineDashType: "shortDash",
      showInLegend: true,
      dataPoints: [],
    },
    {
      name: "Actual Charging (from current)",
      type: "line",
      lineThickness: 2,
      color: "#d32f2f",
      showInLegend: true,
      dataPoints: [],
    },
  ];

  var peerColors = [
    { line: "#4285f4", fill: "rgba(66,133,244,0.35)" },
    { line: "#34a853", fill: "rgba(52,168,83,0.35)" },
    { line: "#fbbc05", fill: "rgba(251,188,5,0.35)" },
    { line: "#f4511e", fill: "rgba(244,81,30,0.35)" },
    { line: "#8e24aa", fill: "rgba(142,36,170,0.35)" },
    { line: "#00acc1", fill: "rgba(0,172,193,0.35)" },
  ];

  var peerSeriesIndex = {};
  peerIds.forEach(function (peerId, idx) {
    var palette = peerColors[idx % peerColors.length];

    var availableIdx = series.length;
    series.push({
      name: peerId + " Available",
      type: "line",
      lineThickness: 1.5,
      lineDashType: "dot",
      color: palette.line,
      showInLegend: true,
      dataPoints: [],
    });

    var consumingIdx = series.length;
    series.push({
      name: peerId + " Consuming",
      type: "stackedArea",
      lineThickness: 1,
      color: palette.fill,
      showInLegend: true,
      dataPoints: [],
    });

    peerSeriesIndex[peerId] = {
      available: availableIdx,
      consuming: consumingIdx,
    };
  });

  rows.forEach(function (row) {
    var x = parseTimeValue(row.Time);

    series[0].dataPoints.push({ x: x, y: parseFloat(row.Available_Pwr_W) });
    series[1].dataPoints.push({ x: x, y: parseFloat(row.Total_Demand_W) });
    series[2].dataPoints.push({ x: x, y: parseFloat(row.Total_EV_Power_W) });

    peerIds.forEach(function (peerId) {
      var idx = peerSeriesIndex[peerId];
      var availablePowerW = parseFloat(row[peerId + "_available_power_w"] || "0");
      var actualPowerW = parseFloat(row[peerId + "_actual_power_w"] || "0");

      series[idx.available].dataPoints.push({ x: x, y: availablePowerW });
      series[idx.consuming].dataPoints.push({ x: x, y: actualPowerW });
    });
  });

  return series;
}

function computeLoadSharingSummaryFromCsv(csv) {
  var parsed = parseCsvWithHeader(csv);
  var headers = parsed.headers;
  var rows = parsed.rows;

  if (rows.length === 0) {
    return null;
  }

  var peerSocColumns = headers.filter(function (name) {
    return name.endsWith("_soc");
  });

  var first = rows[0];
  var last = rows[rows.length - 1];
  var startTime = parseFloat(first.Time);
  var endTime = parseFloat(last.Time);
  var duration = Math.max(0, endTime - startTime);

  var peakBudgetKw = 0;
  var peakDemandKw = 0;
  var peakEvKw = 0;
  var violationCount = 0;

  rows.forEach(function (row) {
    var budgetKw = parseFloat(row.Max_Pwr_W) / 1000.0;
    var demandKw = parseFloat(row.Total_Demand_W) / 1000.0;
    var evKw = parseFloat(row.Total_EV_Power_W) / 1000.0;

    if (budgetKw > peakBudgetKw) {
      peakBudgetKw = budgetKw;
    }
    if (demandKw > peakDemandKw) {
      peakDemandKw = demandKw;
    }
    if (evKw > peakEvKw) {
      peakEvKw = evKw;
    }
    if (parseFloat(row.Total_Demand_W) > parseFloat(row.Max_Pwr_W) * 1.001) {
      violationCount += 1;
    }
  });

  var finalSoc = peerSocColumns.map(function (col) {
    return col.replace("_soc", "") + ": " + parseFloat(last[col]).toFixed(1) + "%";
  }).join(" | ");

  return {
    peer_count: peerSocColumns.length,
    duration_s: duration,
    peak_budget_kw: peakBudgetKw,
    peak_demand_kw: peakDemandKw,
    peak_ev_kw: peakEvKw,
    violation_count: violationCount,
    final_soc: finalSoc,
  };
}

function formatDuration(seconds) {
  return (new Date(parseInt(seconds, 10) * 1000).toISOString().slice(11, 19));
}

function loadChart(id, csv, title, type) {
  $.get(csv, (data) => {
    if ("loadsharing" === type) {
      var series = getLoadSharingSeriesFromCSV(data);
      var chart = new CanvasJS.Chart(id, {
        animationEnabled: true,
        zoomEnabled: true,
        toolTip: {
          shared: true,
          contentFormatter: (e) => {
            var str = "<strong>" + moment(e.entries[0].dataPoint.x).format('HH:mm:ss') + "</strong> <br/>";
            for (var i = 0; i < e.entries.length; i++) {
              str += "<span style=\"color:" + e.entries[i].dataSeries.color + "\">" + e.entries[i].dataSeries.name + "</span> <strong>" + e.entries[i].dataPoint.y.toFixed(2) + "</strong> <br/>";
            }
            return str;
          }
        },
        title: {
          text: title,
          fontSize: 20
        },
        legend: {
          fontSize: 16
        },
        axisY: {
          title: "Power (W)",
          minimum: 0,
          labelFontSize: 14,
          labelAngle: 0
        },
        axisX: {
          valueFormatString: "HH:mm:ss",
          labelFontSize: 14,
          labelAngle: 0
        },
        data: series
      });

      chart.render();
      return;
    }

    var points = getDataPointsFromCSV(data);
    var opts = {
      animationEnabled: true,
      zoomEnabled: true,
      toolTip: {
        shared: true,
        contentFormatter: (e) => {

          var str = "<strong>"+moment(e.entries[0].dataPoint.x).format('h:mm a') + "</strong> <br/>";
          for (var i = 0; i < e.entries.length; i++){
            str += "<span style=\"color:"+e.entries[i].dataSeries.color+"\">" + e.entries[i].dataSeries.name + "</span> <strong>"+  e.entries[i].dataPoint.y + "</strong> <br/>";
          }
          return (str);
        }
      },
      title: {
        text: title,
        fontSize: 20
      },
      legend: {
        fontSize: 16
      },
      axisY: {
        minimum: 0,
        labelFontSize: 14,
        labelAngle: 0
      },
      axisX: {
        labelFontSize: 14,
        labelAngle: 0
      },
      data: []
    }
    opts.data.push({
      name: "Charge Power",
      type: "area",
      color: "rgba(244,180,0,0.7)",
      showInLegend: true,
      dataPoints: points[3]
    });
    if("gridie" === type || "solar" === type) {
      opts.data.push({
        name: "Solar",
        type: "line",
        lineThickness: 1,
        showInLegend: true,
        dataPoints: points[0]
      });
      if("gridie" === type) {
        opts.data.push({
          name: "Grid IE",
          type: "line",
          lineThickness: 1,
          showInLegend: true,
          dataPoints: points[1]
        });
        opts.data.push({
          name: "Smoothed",
          type: "line",
          lineThickness: 1,
          showInLegend: true,
          dataPoints: points[6]
        });
        opts.data.push({
          name: "Min Charge",
          type: "line",
          lineThickness: 1,
          lineDashType: "shortDash",
          lineColor: "#38761d",
          showInLegend: true,
          dataPoints: points[4]
        });
        opts.data.push({
          name: "Min Grid IE",
          type: "line",
          lineThickness: 1,
          lineDashType: "shortDash",
          lineColor: "#38761d",
          showInLegend: true,
          dataPoints: points[2]
        });
      }
    }
    if("shaper" == type) {
      opts.data.push({
        name: "Live Power (Smoothed)",
        type: "line",
        lineThickness: 1,
        showInLegend: true,
        dataPoints: points[8]
      });
      opts.data.push({
        name: "Live Power",
        type: "line",
        lineThickness: 1,
        showInLegend: true,
        dataPoints: points[7]
      });
      opts.data.push({
        name: "Max Power",
        type: "line",
        lineThickness: 1,
        showInLegend: true,
        dataPoints: points[9]
      });
    }

    var chart = new CanvasJS.Chart(id, opts);

    chart.render();
  });

};

function loadLoadSharingSummary(datasets, success) {
  if (!datasets || datasets.length === 0) {
    success();
    return;
  }

  var remaining = datasets.length;
  datasets.forEach(function (dataset) {
    var csv = "output/" + dataset.id + ".csv";
    $.get(csv, function (data) {
      var summaryRow = computeLoadSharingSummaryFromCsv(data);
      if (summaryRow) {
        loadsharing_summary[dataset.id] = summaryRow;
      }
    }).always(function () {
      remaining -= 1;
      if (remaining === 0) {
        success();
      }
    });
  });
}

function loadSummary(csv, success, profile = false) {
  $.ajax({
    url: csv,
    dataType: "text",
    success: function (data) {
      var summary_data = data.split(/\r?\n|\r/);
      for (var count = 1; count < summary_data.length; count++) {
        var cell_data = summary_data[count].split(",");
        if(cell_data.length < 2) {
          continue;
        }

        var dataset = cell_data[0].replaceAll("\"", "");
        var config = false === profile ? cell_data[1].replaceAll("\"", "").replaceAll("data/config-inputfilter-", "").replaceAll("data/config-shaper-", "").replaceAll(".json", "") : profile;

        var data = {
          total_solar: parseFloat(cell_data[2]).toFixed(2),
          total_ev_charged: parseFloat(cell_data[3]).toFixed(2),
          charge_from_solar: parseFloat(cell_data[4]).toFixed(2),
          charge_from_grid: parseFloat(cell_data[5]).toFixed(2),
          number_of_charges: parseInt(cell_data[6]),
          min_time_charging: (new Date(parseInt(cell_data[7]) * 1000).toISOString().slice(11, 19)),
          max_time_charging: (new Date(parseInt(cell_data[8]) * 1000).toISOString().slice(11, 19)),
          total_time_charging: (new Date(parseInt(cell_data[9]) * 1000).toISOString().slice(11, 19))
        }

        summary[config.toLowerCase()][dataset] = data;
      }

      success();
    }
  });
}

function generate_summary_table_rows(profiles, datasets) {
  var table_data = '';
  for (const dataset of datasets) {
    for (const profile of profiles) {
      var data = summary[profile][dataset.id];
      table_data += '<tr class="' + dataset.class + '">';
      table_data += '<td><a href="#' + dataset.id + "_" + profile + '">' + dataset.title + '</a></td>';
      table_data += '<td>' + profile + '</td>';
      table_data += '<td>' + data.total_solar + '</td>';
      table_data += '<td>' + data.total_ev_charged + '</td>';
      table_data += '<td>' + data.charge_from_solar + '</td>';
      table_data += '<td>' + data.charge_from_grid + '</td>';
      table_data += '<td>' + data.number_of_charges + '</td>';
      table_data += '<td>' + data.min_time_charging + '</td>';
      table_data += '<td>' + data.max_time_charging + '</td>';
      table_data += '<td>' + data.total_time_charging + '</td>';
      table_data += '</tr>';
    }
  }
  return table_data;
}

function generate_summary_table(divert_profiles, shaper_profiles) {
  var table_data = '<table class="table table-bordered table-striped">';
  table_data += '<tr>';
  table_data += '<th>Dataset</th>';
  table_data += '<th>Config</th>';
  table_data += '<th>Total Solar</th>';
  table_data += '<th>Total EV Charged</th>';
  table_data += '<th>Charge from Solar</th>';
  table_data += '<th>Charge from Grid</th>';
  table_data += '<th>Number of Charges</th>';
  table_data += '<th>Min Time Charging</th>';
  table_data += '<th>Max Time Charging</th>';
  table_data += '<th>Total Time Charging</th>';
  table_data += '</tr>';
  table_data += generate_summary_table_rows(divert_profiles, divert_datasets);
  table_data += generate_summary_table_rows(shaper_profiles, shaper_datasets);
  table_data += '</table>';

  if (Object.keys(loadsharing_summary).length > 0) {
    table_data += '<h3>Load Sharing Summary</h3>';
    table_data += '<table class="table table-bordered table-striped">';
    table_data += '<tr>';
    table_data += '<th>Scenario</th>';
    table_data += '<th>Peers</th>';
    table_data += '<th>Duration</th>';
    table_data += '<th>Peak Budget (kW)</th>';
    table_data += '<th>Peak Demand (kW)</th>';
    table_data += '<th>Peak EV Power (kW)</th>';
    table_data += '<th>Supply Violations</th>';
    table_data += '<th>Final SoC</th>';
    table_data += '</tr>';

    for (const dataset of loadsharing_datasets) {
      var lsData = loadsharing_summary[dataset.id];
      if (!lsData) {
        continue;
      }

      table_data += '<tr class="' + dataset.class + '">';
      table_data += '<td><a href="#' + dataset.id + '_scenario">' + dataset.title + '</a></td>';
      table_data += '<td>' + lsData.peer_count + '</td>';
      table_data += '<td>' + formatDuration(lsData.duration_s) + '</td>';
      table_data += '<td>' + lsData.peak_budget_kw.toFixed(2) + '</td>';
      table_data += '<td>' + lsData.peak_demand_kw.toFixed(2) + '</td>';
      table_data += '<td>' + lsData.peak_ev_kw.toFixed(2) + '</td>';
      table_data += '<td>' + lsData.violation_count + '</td>';
      table_data += '<td>' + lsData.final_soc + '</td>';
      table_data += '</tr>';
    }

    table_data += '</table>';
  }

  $('#summary_table').html(table_data);
}

function generate_chart(dataset, profile)
{
  var id = dataset.id + "_" + profile;
  var div = document.createElement("div");
  div.id = id;
  div.className = dataset.class;
  document.body.appendChild(div);
  return id;
}

function toggleCharts() {
  var showDivertInput = document.getElementById('show_divert');
  var showShaperInput = document.getElementById('show_shaper');
  var showLoadSharingInput = document.getElementById('show_loadsharing');

  var showDivert = showDivertInput ? showDivertInput.checked : true;
  var showShaper = showShaperInput ? showShaperInput.checked : true;
  var showLoadSharing = showLoadSharingInput ? showLoadSharingInput.checked : true;

  if (showDivert) {
    document.body.classList.remove('hide-divert');
  } else {
    document.body.classList.add('hide-divert');
  }

  if (showShaper) {
    document.body.classList.remove('hide-shaper');
  } else {
    document.body.classList.add('hide-shaper');
  }

  if (showLoadSharing) {
    document.body.classList.remove('hide-loadsharing');
  } else {
    document.body.classList.add('hide-loadsharing');
  }
}

// =============================================================================
// view.html — tab-based lazy loading (interactive.html uses the older API above)
// =============================================================================

var csvDataCache = {};          // url -> fetched CSV text
var currentDivertProfile = null;
var currentShaperProfile = null;

// ---------- Initialisation ---------------------------------------------------

function initPage(divertProfiles, shaperProfiles) {
  init_summary(divertProfiles, shaperProfiles);
  populateProfileSelects(divertProfiles, shaperProfiles);
  setupTabHandlers();
  // Load all summaries in parallel; auto-start first tab's charts when done.
  loadAllSummaries(divertProfiles, shaperProfiles);
}

function populateProfileSelects(divertProfiles, shaperProfiles) {
  var $divert = $('#divert-profile-select');
  divertProfiles.forEach(function (p) { $divert.append($('<option>').val(p).text(p)); });

  var $shaper = $('#shaper-profile-select');
  shaperProfiles.forEach(function (p) { $shaper.append($('<option>').val(p).text(p)); });

  currentDivertProfile = divertProfiles[0];
  currentShaperProfile = shaperProfiles[0];
}

function setupTabHandlers() {
  $(document).on('click', '.sim-tab-nav a[data-tab]', function (e) {
    e.preventDefault();
    switchTab($(this).data('tab'));
  });
}

function switchTab(tab) {
  $('.sim-tab-nav li').removeClass('active');
  $('.sim-tab-nav a[data-tab="' + tab + '"]').parent().addClass('active');
  $('.tab-content .tab-pane').removeClass('active');
  $('#tab-' + tab).addClass('active');

  if (tab === 'divert') {
    loadTabCharts('divert', currentDivertProfile);
  } else if (tab === 'shaper') {
    loadTabCharts('shaper', currentShaperProfile);
  } else if (tab === 'loadsharing') {
    loadTabCharts('loadsharing', null);
  }
}

// ---------- Parallel summary loading ----------------------------------------

/**
 * Returns a jQuery Deferred that resolves once the summary CSV is parsed into
 * the global `summary` object.  Never rejects — missing files are silently
 * skipped so one bad file cannot block the whole batch.
 */
function loadSummaryP(url, profile) {
  var def = $.Deferred();
  $.ajax({
    url: url,
    dataType: 'text',
    success: function (data) {
      var lines = data.split(/\r?\n|\r/);
      for (var i = 1; i < lines.length; i++) {
        var cols = lines[i].split(',');
        if (cols.length < 2) { continue; }
        var dataset = cols[0].replace(/"/g, '');
        var config = (profile !== false)
          ? profile
          : cols[1].replace(/"/g, '')
                   .replace(/data\/config-(inputfilter|shaper)-/, '')
                   .replace('.json', '');
        var key = config.toLowerCase();
        if (!summary[key]) { summary[key] = {}; }
        summary[key][dataset] = {
            total_solar:        parseFloat(cols[2]).toFixed(2),
            total_ev_charged:   parseFloat(cols[3]).toFixed(2),
            charge_from_solar:  parseFloat(cols[4]).toFixed(2),
            charge_from_grid:   parseFloat(cols[5]).toFixed(2),
            number_of_charges:  parseInt(cols[6]),
            min_time_charging:  new Date(parseInt(cols[7])  * 1000).toISOString().slice(11, 19),
            max_time_charging:  new Date(parseInt(cols[8])  * 1000).toISOString().slice(11, 19),
            total_time_charging:new Date(parseInt(cols[9])  * 1000).toISOString().slice(11, 19),
          };
          }
      def.resolve();
    },
    error: function () { def.resolve(); },
  });
  return def.promise();
}

/**
 * Fetches all load-sharing CSVs in parallel, populates loadsharing_summary
 * AND primes csvDataCache so that the Load Sharing tab charts render instantly
 * from cache (no second network trip).
 */
function loadLoadSharingSummaryP(datasets) {
  if (!datasets || datasets.length === 0) { return $.when(); }

  var fetches = datasets.map(function (dataset) {
    var url = 'output/' + dataset.id + '.csv';
    return $.get(url)
      .then(function (data) {
        csvDataCache[url] = data;
        var row = computeLoadSharingSummaryFromCsv(data);
        if (row) { loadsharing_summary[dataset.id] = row; }
      }, function () {});  // ignore 404s
  });

  return $.when.apply($, fetches);
}

function loadAllSummaries(divertProfiles, shaperProfiles) {
  $.when(
    loadSummaryP('output/summary_divert_master.csv', false),
    loadSummaryP('output/summary_divert.csv',        false),
    loadSummaryP('output/summary_shaper.csv',        false),
    loadSummaryP('output/summary_shaper_master.csv', false),
    loadLoadSharingSummaryP(loadsharing_datasets)
  ).then(function () {
    renderDivertSummaryTable(divertProfiles);
    renderShaperSummaryTable(shaperProfiles);
    renderLoadSharingSummaryTable();
    // Auto-start loading the initially-active tab's charts.
    loadTabCharts('divert', currentDivertProfile);
  });
}

// ---------- Summary table rendering -----------------------------------------

function buildDivertShaperSummaryTable(profiles, datasets) {
  var html = '<div class="table-responsive">';
  html += '<table class="table table-bordered table-striped table-condensed sim-summary-table">';
  html += '<thead><tr>';
  html += '<th>Dataset</th><th>Config</th>';
  html += '<th>Total Solar</th><th>Total EV Charged</th>';
  html += '<th>From Solar</th><th>From Grid</th>';
  html += '<th>Charges</th>';
  html += '<th>Min Charging</th><th>Max Charging</th><th>Total Charging</th>';
  html += '</tr></thead><tbody>';
  for (var d = 0; d < datasets.length; d++) {
    var dataset = datasets[d];
    for (var p = 0; p < profiles.length; p++) {
      var profile = profiles[p];
      var row = summary[profile] && summary[profile][dataset.id];
      if (!row || !row.total_solar) { continue; }
      html += '<tr>';
      html += '<td>' + dataset.title + '</td>';
      html += '<td>' + profile + '</td>';
      html += '<td>' + row.total_solar + '</td>';
      html += '<td>' + row.total_ev_charged + '</td>';
      html += '<td>' + row.charge_from_solar + '</td>';
      html += '<td>' + row.charge_from_grid + '</td>';
      html += '<td>' + row.number_of_charges + '</td>';
      html += '<td>' + row.min_time_charging + '</td>';
      html += '<td>' + row.max_time_charging + '</td>';
      html += '<td>' + row.total_time_charging + '</td>';
      html += '</tr>';
    }
  }
  html += '</tbody></table></div>';
  return html;
}

function renderDivertSummaryTable(profiles) {
  $('#summary-divert')
    .html(buildDivertShaperSummaryTable(profiles, divert_datasets))
    .removeClass('sim-summary-placeholder');
}

function renderShaperSummaryTable(profiles) {
  $('#summary-shaper')
    .html(buildDivertShaperSummaryTable(profiles, shaper_datasets))
    .removeClass('sim-summary-placeholder');
}

function renderLoadSharingSummaryTable() {
  if (Object.keys(loadsharing_summary).length === 0) {
    $('#summary-loadsharing')
      .html('<p class="text-muted">No load sharing results found.</p>')
      .removeClass('sim-summary-placeholder');
    return;
  }

  var html = '<div class="table-responsive">';
  html += '<table class="table table-bordered table-striped table-condensed sim-summary-table">';
  html += '<thead><tr>';
  html += '<th>Scenario</th><th>Peers</th><th>Duration</th>';
  html += '<th>Peak Budget (kW)</th><th>Peak Demand (kW)</th><th>Peak EV Power (kW)</th>';
  html += '<th>Supply Violations</th><th>Final SoC</th>';
  html += '</tr></thead><tbody>';

  for (var i = 0; i < loadsharing_datasets.length; i++) {
    var dataset = loadsharing_datasets[i];
    var d = loadsharing_summary[dataset.id];
    if (!d) { continue; }
    var violations = d.violation_count > 0
      ? '<span class="text-danger"><strong>' + d.violation_count + '</strong></span>'
      : '0';
    html += '<tr>';
    html += '<td>' + dataset.title + '</td>';
    html += '<td>' + d.peer_count + '</td>';
    html += '<td>' + formatDuration(d.duration_s) + '</td>';
    html += '<td>' + d.peak_budget_kw.toFixed(2) + '</td>';
    html += '<td>' + d.peak_demand_kw.toFixed(2) + '</td>';
    html += '<td>' + d.peak_ev_kw.toFixed(2) + '</td>';
    html += '<td>' + violations + '</td>';
    html += '<td>' + d.final_soc + '</td>';
    html += '</tr>';
  }

  html += '</tbody></table></div>';
  $('#summary-loadsharing')
    .html(html)
    .removeClass('sim-summary-placeholder');
}

// ---------- Lazy chart loading ----------------------------------------------

function onDivertProfileChange() {
  currentDivertProfile = $('#divert-profile-select').val();
  loadTabCharts('divert', currentDivertProfile);
}

function onShaperProfileChange() {
  currentShaperProfile = $('#shaper-profile-select').val();
  loadTabCharts('shaper', currentShaperProfile);
}

function loadTabCharts(type, profile) {
  var $container = $('#' + type + '-charts-container');
  if ($container.length === 0) { return; }

  var datasets, items;

  if (type === 'loadsharing') {
    // Load sharing: CSV filename differs from chart div ID
    items = loadsharing_datasets.map(function (dataset) {
      return {
        dataset: dataset,
        id:      dataset.id + '_scenario',
        url:     'output/' + dataset.id + '.csv',   // no "_scenario" in filename
        suffix:  'scenario',
      };
    });
  } else {
    if (!profile) { return; }
    datasets = (type === 'divert') ? divert_datasets : shaper_datasets;
    items = datasets.map(function (dataset) {
      var id = dataset.id + '_' + profile;
      return {
        dataset: dataset,
        id:      id,
        url:     'output/' + id + '.csv',
        suffix:  profile,
      };
    });
  }

  // If all CSV data is already cached, render immediately.
  if (items.every(function (item) { return !!csvDataCache[item.url]; })) {
    renderChartItems(items, $container);
    return;
  }

  $container.html(
    '<div class="sim-chart-loading">' +
      '<span class="glyphicon glyphicon-refresh sim-spinning"></span> Loading charts&hellip;' +
    '</div>'
  );

  // Fetch all missing CSVs in parallel, render when the last one arrives.
  var pending = items.length;
  items.forEach(function (item) {
    if (csvDataCache[item.url]) {
      if (--pending === 0) { renderChartItems(items, $container); }
      return;
    }
    $.get(item.url)
      .done(function (data) { csvDataCache[item.url] = data; })
      .always(function ()   { if (--pending === 0) { renderChartItems(items, $container); } });
  });
}

function renderChartItems(items, $container) {
  $container.empty();
  items.forEach(function (item) {
    var data = csvDataCache[item.url];
    if (!data) { return; }
    var div = document.createElement('div');
    div.id        = item.id;
    div.className = item.dataset.class;
    $container[0].appendChild(div);
    renderChart(item.id, data, item.dataset.title + ' (' + item.suffix + ')', item.dataset.class);
  });
}

/**
 * Renders a CanvasJS chart from pre-fetched CSV text.
 * Differs from loadChart() in that it takes raw data (not a URL) and returns
 * the chart object.  animationEnabled is off for faster initial paint.
 */
function renderChart(id, data, title, type) {
  if (type === 'loadsharing') {
    var series = getLoadSharingSeriesFromCSV(data);
    var chart = new CanvasJS.Chart(id, {
      animationEnabled: false,
      zoomEnabled: true,
      toolTip: {
        shared: true,
        contentFormatter: function (e) {
          var str = '<strong>' + moment(e.entries[0].dataPoint.x).format('HH:mm:ss') + '</strong><br/>';
          for (var i = 0; i < e.entries.length; i++) {
            str += '<span style="color:' + e.entries[i].dataSeries.color + '">' +
                   e.entries[i].dataSeries.name + '</span> <strong>' +
                   e.entries[i].dataPoint.y.toFixed(2) + '</strong><br/>';
          }
          return str;
        },
      },
      title:  { text: title, fontSize: 18 },
      legend: { fontSize: 14 },
      axisY:  { title: 'Power (W)', minimum: 0, labelFontSize: 13 },
      axisX:  { valueFormatString: 'HH:mm:ss', labelFontSize: 13 },
      data:   series,
    });
    chart.render();
    return chart;
  }

  var points = getDataPointsFromCSV(data);
  var opts = {
    animationEnabled: false,
    zoomEnabled: true,
    toolTip: {
      shared: true,
      contentFormatter: function (e) {
        var str = '<strong>' + moment(e.entries[0].dataPoint.x).format('h:mm a') + '</strong><br/>';
        for (var i = 0; i < e.entries.length; i++) {
          str += '<span style="color:' + e.entries[i].dataSeries.color + '">' +
                 e.entries[i].dataSeries.name + '</span> <strong>' +
                 e.entries[i].dataPoint.y + '</strong><br/>';
        }
        return str;
      },
    },
    title:  { text: title, fontSize: 18 },
    legend: { fontSize: 14 },
    axisY:  { minimum: 0, labelFontSize: 13 },
    axisX:  { labelFontSize: 13 },
    data:   [],
  };

  opts.data.push({
    name: 'Charge Power', type: 'area',
    color: 'rgba(244,180,0,0.7)', showInLegend: true, dataPoints: points[3],
  });

  if (type === 'gridie' || type === 'solar') {
    opts.data.push({ name: 'Solar', type: 'line', lineThickness: 1, showInLegend: true, dataPoints: points[0] });
    if (type === 'gridie') {
      opts.data.push({ name: 'Grid IE',   type: 'line', lineThickness: 1, showInLegend: true, dataPoints: points[1] });
      opts.data.push({ name: 'Smoothed',  type: 'line', lineThickness: 1, showInLegend: true, dataPoints: points[6] });
      opts.data.push({ name: 'Min Charge',  type: 'line', lineThickness: 1, lineDashType: 'shortDash', lineColor: '#38761d', showInLegend: true, dataPoints: points[4] });
      opts.data.push({ name: 'Min Grid IE', type: 'line', lineThickness: 1, lineDashType: 'shortDash', lineColor: '#38761d', showInLegend: true, dataPoints: points[2] });
    }
  }

  if (type === 'shaper') {
    opts.data.push({ name: 'Live Power (Smoothed)', type: 'line', lineThickness: 1, showInLegend: true, dataPoints: points[8] });
    opts.data.push({ name: 'Live Power',            type: 'line', lineThickness: 1, showInLegend: true, dataPoints: points[7] });
    opts.data.push({ name: 'Max Power',             type: 'line', lineThickness: 1, showInLegend: true, dataPoints: points[9] });
  }

  var chart = new CanvasJS.Chart(id, opts);
  chart.render();
  return chart;
}
