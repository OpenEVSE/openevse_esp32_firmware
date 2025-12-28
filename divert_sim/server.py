"""
Simple HTTP server to run dynamic simulations and view results.
"""

import http.server
import socketserver
import simplejson as json

from run_simulations import run_simulation, setup_summary

class MyHttpRequestHandler(http.server.SimpleHTTPRequestHandler):
    """
    This class will handles any incoming request from the browser
    """
    def do_GET(self):
        if self.path == '/':
            self.path = 'view.html'
        return http.server.SimpleHTTPRequestHandler.do_GET(self)

    def do_POST(self):
        content_length = int(self.headers['Content-Length']) # <--- Gets the size of data
        post_data = self.rfile.read(content_length) # <--- Gets the data itself
        self.send_response(200)
        self.end_headers()

        try:
          config = json.loads(post_data)
        except ValueError as e:
          config = {}
        print("{}".format(config))

        # Run the simulation
        setup_summary('_interactive')
        run_simulation('almostperfect', 'almostperfect_interactive',
                    solar_col=1, config=json.dumps(config))

        run_simulation('CloudyMorning', 'CloudyMorning_interactive',
                    solar_col=1, config=json.dumps(config))

        run_simulation('day1', 'day1_interactive',
                    solar_col=1, config=json.dumps(config))

        run_simulation('day2', 'day2_interactive',
                    solar_col=1, config=json.dumps(config))

        run_simulation('day3', 'day3_interactive',
                    solar_col=1, config=json.dumps(config))

        run_simulation('day1_grid_ie', 'day1_grid_ie_interactive',
                    solar_col=1, grid_ie_col=2, config=json.dumps(config))

        run_simulation('day2_grid_ie', 'day2_grid_ie_interactive',
                    solar_col=1, grid_ie_col=2, config=json.dumps(config))

        run_simulation('day3_grid_ie', 'day3_grid_ie_interactive',
                    solar_col=1, grid_ie_col=2, config=json.dumps(config))

        run_simulation('solar-vrms', 'solar-vrms_interactive',
                    solar_col=1, voltage_col=2, config=json.dumps(config))

        run_simulation('Energy_and_Power_Day_2020-03-22', 'Energy_and_Power_Day_2020-03-22_interactive',
                    solar_col=1, separator=';', is_kw=True, config=json.dumps(config))

        run_simulation('Energy_and_Power_Day_2020-03-31', 'Energy_and_Power_Day_2020-03-31_interactive',
                    solar_col=1, separator=';', is_kw=True, config=json.dumps(config))

        run_simulation('Energy_and_Power_Day_2020-04-01', 'Energy_and_Power_Day_2020-04-01_interactive',
                    solar_col=1, separator=';', is_kw=True, config=json.dumps(config))

        run_simulation('data_shaper', 'data_shaper_interactive',
                    live_power_col=1, separator=';', config=json.dumps(config))

        self.wfile.write("OK".encode('utf-8'))


# Create an object of the above class
handler_object = MyHttpRequestHandler

PORT = 8000
my_server = socketserver.TCPServer(("", PORT), handler_object)

# Start the server
print("Server started at localhost:" + str(PORT))
my_server.serve_forever()
