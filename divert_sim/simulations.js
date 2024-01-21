var datasets = [
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
  { id: "data_shaper", class: "shapper", title: "Shapper example 1" },
];

var summary = {};
function init_summary(profiles) {
  for (const profile of profiles) {
    summary[profile] = {};
    for (const dataset of datasets) {
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

      var date = moment(points[0], "DD/MM/YYYY HH:mm:ss").toDate();
      for (var p = 1; p < points.length; p++) {
        dataPoints[p - 1].push({
          x: date,
          y: parseFloat(points[p])
        });
      }
    }
  return dataPoints;
}

function loadChart(id, csv, title, type) {
  $.get(csv, (data) => {
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
    if("shapper" == type) {
      opts.data.push({
        name: "Live Power",
        type: "line",
        lineThickness: 1,
        showInLegend: true,
        dataPoints: points[7]
      });
      opts.data.push({
        name: "Live Power (Smoothed)",
        type: "line",
        lineThickness: 1,
        showInLegend: true,
        dataPoints: points[8]
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
        config = false === profile ? cell_data[1].replaceAll("\"", "").replaceAll("data/config-inputfilter-", "").replaceAll(".json", "") : profile;

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

function generate_summary_table(profiles) {
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
  for (const dataset of datasets) {
    for (const profile of profiles) {
      var data = summary[profile][dataset.id];
      table_data += '<tr>';
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
  table_data += '</table>';
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
