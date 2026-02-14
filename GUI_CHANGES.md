# GUI Changes Required for RFID User Management and CSV Export

## Overview
The frontend changes need to be implemented in the [openevse-gui-v2](https://github.com/OpenEVSE/openevse-gui-v2) repository to support RFID user name assignment and CSV export functionality.

## Required Changes

### 1. File: `src/components/blocks/configuration/Rfid.svelte`

#### Add RFID User Name Management

**Location**: After the existing RFID tag registration functionality

**Changes needed**:
1. Add state variables for RFID user mappings and editing:
```javascript
let rfidUsers = {}
let editingTag = null
let editingName = ""
```

2. Add function to load RFID users on mount:
```javascript
async function loadRfidUsers() {
    const users = await serialQueue.add(() => httpAPI("GET", "/rfid/users"))
    if (users && users !== "error") {
        rfidUsers = users
    }
}
```

3. Add functions for user name editing:
```javascript
function startEditName(tag) {
    editingTag = tag
    editingName = rfidUsers[tag] || ""
}

function cancelEditName() {
    editingTag = null
    editingName = ""
}

async function saveUserName() {
    if (!editingTag) return
    
    const jsondata = {
        rfid: editingTag,
        name: editingName
    }
    
    let res = await serialQueue.add(() => httpAPI("POST", "/rfid/users", JSON.stringify(jsondata)))
    if (res && res !== "error") {
        rfidUsers[editingTag] = editingName
        editingTag = null
        editingName = ""
    }
}
```

4. Update the onMount to load user mappings:
```javascript
onMount(() => {
    updateTags($config_store.rfid_storage)
    loadRfidUsers()  // Add this line
    updateFormData()
    mounted = true
})
```

5. Replace the registered tags display section (starting around line 166) with enhanced UI:
```svelte
{#if tags.length > 0 }
<div class="columns is-centered m-0 pb-1">
    <div class="column is-two-thirds m-0">
        <div class="is-flex is-justify-content-center">
            <Borders grow>
                <div class="has-text-weight-bold is-size-6 has-text-centered mb-2">{$_("config.rfid.registeredtags")}</div>
                <div class="has-text-centered mt-4">
                    <Button bind:this={button2_inst} name={$_("config.rfid.removeall")} color="is-danger" butn_submit={()=>removeTag("all",button2_inst)} />
                </div>
                <div class="scrollable my-2">
                    {#each tags as tag,i}
                        <div class="box is-flex is-align-items-center is-justify-content-space-between my-2 p-2" style="background-color: #f5f5f5;">
                            <div class="is-flex-grow-1">
                                <div class="has-text-weight-bold has-text-info">UID: {tag}</div>
                                {#if editingTag === tag}
                                    <div class="field has-addons mt-2">
                                        <div class="control is-expanded">
                                            <input class="input is-small" type="text" bind:value={editingName} placeholder="Enter user name" />
                                        </div>
                                        <div class="control">
                                            <button class="button is-small is-success" on:click={saveUserName}>Save</button>
                                        </div>
                                        <div class="control">
                                            <button class="button is-small" on:click={cancelEditName}>Cancel</button>
                                        </div>
                                    </div>
                                {:else}
                                    <div class="has-text-grey-dark mt-1">
                                        {#if rfidUsers[tag]}
                                            User: {rfidUsers[tag]}
                                            <button class="button is-small is-text" on:click={() => startEditName(tag)}>
                                                <iconify-icon icon="mdi:pencil"></iconify-icon>
                                            </button>
                                        {:else}
                                            <button class="button is-small is-text" on:click={() => startEditName(tag)}>
                                                + Add user name
                                            </button>
                                        {/if}
                                    </div>
                                {/if}
                            </div>
                            <div>
                                <RemovableTag bind:this={tags_inst[i]} name="Remove" action={()=>removeTag(tag,tags_inst[i])} color={$status_store.rfid_input == tag?"is-primary":"is-danger"}/>
                            </div>
                        </div>
                    {/each}
                </div>
            </Borders>
        </div>
    </div>
</div>
{/if}
```

### 2. File: `src/components/blocks/history/Logs.svelte`

#### Add CSV Export and User Name Display

**Changes needed**:

1. Import Button component (add to existing imports):
```javascript
import Button from "./../../ui/Button.svelte";
```

2. Add state variables for RFID users:
```javascript
let rfidUsers = {}
```

3. Update the init function to load RFID users:
```javascript
async function init() {
    index = await serialQueue.add(() => httpAPI("GET","/logs"))
    if ($uistates_store.logidx_min != index.min || $uistates_store.logidx_max != index.max) {
        loaded = false
        $uistates_store.logidx_min = index.min
        $uistates_store.logidx_max = index.max
        $history_store = ""
        for (let i = index.min; i <= index.max; i++) {
            progress = (i - index.min)*100/(index.max-index.min)
            await serialQueue.add(() => history_store.download(i))
        }
        loaded = true
    }
    else {
        progress = 100
        await serialQueue.add(() => history_store.download(index.max))
        loaded = true
    }
    
    // Load RFID user mappings
    const users = await serialQueue.add(() => httpAPI("GET", "/rfid/users"))
    if (users && users !== "error") {
        rfidUsers = users
    }
}
```

4. Add CSV export function:
```javascript
function downloadCSV() {
    window.location.href = '/logs/export'
}
```

5. Update the style section to increase table width:
```css
.table {
    max-width: 800px;  /* Changed from 600px */
}
```

6. Add export button before the table (after the opening Box tag):
```svelte
<Box title={$_("logs-title")} icon="icon-park-outline:history-query">
    <div class="has-text-centered mb-3">
        <Button name="Export CSV" color="is-primary" butn_submit={downloadCSV} />
    </div>
    <div class="has-text-centered is-flex-grow-1 is-flex is-justify-content-center has-text-dark">
```

7. Add User column to the table header (after Energy column):
```svelte
<th class="has-text-centered has-text-dark"><abbr title="{$_("logs-energy")}">{$_("units.kwh")}</abbr></th>
<th class="has-text-centered has-text-dark"><abbr title="RFID User">User</abbr></th>
<th class="has-text-centered has-text-dark"><abbr title={$_("logs-temp")}>{$_("logs-T")}</abbr></th>
```

8. Add User data cell in the table body (after Energy cell):
```svelte
<td class="has-text-weight-bold">{round(item.energy/1000,1).toFixed(1)}</td>
<td class="has-text-grey-dark">
    {#if item.rfidTag}
        {rfidUsers[item.rfidTag] || item.rfidTag}
    {:else}
        -
    {/if}
</td>
<td class="has-text-weight-bold">{round(item.temperature,1).toFixed(1)}</td>
```

## Testing the Changes

After implementing these changes:

1. **RFID User Management**:
   - Navigate to RFID configuration page
   - Register an RFID tag
   - Click "Add user name" and enter a name
   - Verify the name is saved and persists after page refresh

2. **CSV Export**:
   - Navigate to session history page
   - Click "Export CSV" button
   - Verify CSV downloads with session data including user names

3. **Session History Display**:
   - Verify the User column appears in the session history table
   - Check that user names are displayed for sessions with RFID authentication
   - Verify RFID UID is shown when no user name is assigned
   - Verify "-" is shown for sessions without RFID authentication

## API Endpoints Used

These backend endpoints are already implemented in the firmware:
- `GET /rfid/users` - Retrieve all RFID user mappings
- `POST /rfid/users` - Set/update user name (body: `{"rfid": "uid", "name": "John Doe"}`)
- `DELETE /rfid/users?rfid=uid` - Remove user name mapping
- `GET /logs/export` - Download session history as CSV
