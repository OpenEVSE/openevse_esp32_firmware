# Testing Guide for RFID User Mapping and CSV Export Feature

## Prerequisites
- OpenEVSE hardware with RFID reader
- Multiple RFID tags for testing
- Web browser to access the GUI

## Backend Testing

### 1. RFID User Mapping Storage
**Test**: Store and retrieve RFID user mappings
- Start the device and scan an RFID tag
- Use the API to set a user name: `POST /rfid/users` with body `{"rfid": "TAG_ID", "name": "John Doe"}`
- Verify the file `/rfid_users.json` exists on the device filesystem
- Retrieve the mapping: `GET /rfid/users` should return `{"TAG_ID": "John Doe"}`
- Restart the device and verify the mapping persists

### 2. Event Logging with RFID Tags
**Test**: Verify RFID tags are logged with session data
- Scan an RFID tag to authenticate
- Connect a vehicle and charge for a few minutes
- Check event logs via `GET /logs/{index}` 
- Verify the log entries include `"rfid": "TAG_ID"` field
- Test without RFID authentication - verify logs work without the rfid field

### 3. CSV Export Functionality
**Test**: Export session history as CSV
- Navigate to `/logs/export` endpoint
- Verify CSV file downloads with proper headers: `Time,Type,State,Energy (kWh),Elapsed (min),RFID Tag,User Name,Temperature (C)`
- Verify data is properly formatted and escaped
- Test with sessions that have RFID data and without
- Verify user names appear in the CSV when mappings exist

### 4. Backward Compatibility
**Test**: Ensure existing logs work correctly
- Load logs that were created before this feature (without rfid field)
- Verify they display correctly in the UI and export without errors
- Verify new logs with RFID data also work correctly

## Frontend Testing

### 1. RFID Management UI
**Test**: Assign names to RFID tags
- Navigate to RFID configuration page
- Scan a new RFID tag
- Register the tag
- Click "Add user name" for the tag
- Enter a name (e.g., "Test User")
- Click "Save" and verify the name is displayed
- Refresh the page and verify the name persists
- Edit the name and verify changes are saved
- Remove the tag and verify it's deleted from both tag list and user mappings

### 2. Session History Display
**Test**: View RFID user names in session logs
- Navigate to the History/Logs page
- Verify the "User" column is visible in the table
- For sessions with RFID authentication:
  - If user name exists: display the name (e.g., "John Doe")
  - If no user name: display the RFID tag ID
- For sessions without RFID: display "-"
- Verify the table layout is responsive and readable

### 3. CSV Export Button
**Test**: Download session history
- Navigate to the History/Logs page
- Click "Export CSV" button
- Verify file downloads as `session_history.csv`
- Open in spreadsheet application (Excel, Google Sheets, etc.)
- Verify all columns are present and data is properly formatted
- Verify user names appear in the CSV

## API Testing

Use a tool like curl, Postman, or the VS Code REST Client extension with the test files in `test/*.http`:

### GET /rfid/users
```bash
curl -X GET http://openevse.local/rfid/users
# Expected: {"TAG_ID_1": "User 1", "TAG_ID_2": "User 2"}
```

### POST /rfid/users
```bash
curl -X POST http://openevse.local/rfid/users \
  -H "Content-Type: application/json" \
  -d '{"rfid": "ABC123", "name": "Test User"}'
# Expected: {"msg":"User name saved"}
```

### DELETE /rfid/users
```bash
curl -X DELETE "http://openevse.local/rfid/users?rfid=ABC123"
# Expected: {"msg":"User name removed"}
```

### GET /logs/export
```bash
curl -X GET http://openevse.local/logs/export > sessions.csv
# Expected: CSV file with session history
```

## Edge Cases to Test

1. **Special characters in names**: Test with names containing commas, quotes, newlines
2. **Empty user names**: Save an empty name, verify it removes the mapping
3. **Very long names**: Test with 100+ character names
4. **Unicode characters**: Test with emoji and international characters
5. **Concurrent access**: Multiple users editing user names simultaneously
6. **Large CSV exports**: Test with 1000+ session entries
7. **Missing RFID reader**: Verify system works when RFID is disabled
8. **Session without vehicle**: Start session with RFID but no vehicle connected

## Expected Results

### Success Criteria
- ✅ RFID user mappings persist across reboots
- ✅ CSV export includes all session data with proper formatting
- ✅ User names display correctly in the UI
- ✅ Backward compatibility maintained with existing logs
- ✅ No memory leaks or crashes when using the feature
- ✅ API endpoints return proper error messages for invalid input

### Performance Criteria
- CSV export should complete in < 5 seconds for 100 sessions
- RFID user lookup should be instant (< 100ms)
- UI should remain responsive when loading logs

## Known Limitations

1. RFID user mappings are stored locally on the device (not synced across multiple devices)
2. CSV export includes all sessions (no date range filtering yet - can be added if needed)
3. Maximum recommended user mappings: ~100 (limited by JSON document size of 2048 bytes)
4. CSV export streams entire history - very large log files may take time to download
