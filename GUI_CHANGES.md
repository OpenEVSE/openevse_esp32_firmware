# GUI Changes for Button Enable/Disable Feature

This file documents the changes made to the OpenEVSE GUI (gui-v2 submodule) to add the button enable/disable toggle.

## Note
The GUI is maintained in a separate repository (https://github.com/OpenEVSE/openevse-gui-v2).
The changes below should be upstreamed to that repository in a separate PR.

## Files Modified

### 1. src/components/blocks/configuration/Evse.svelte

**Changes made:**
- Added `button_enabled` field to formdata object (line 33)
- Added initialization of button_enabled in updateFormData() function (line 51)
- Added new Switch component for button enable/disable after the pause status section (lines 252-267)

**New UI Section:**
```svelte
<div class="my-1 is-flex is-justify-content-center" >
    <Borders grow={true} has_help={true}>
        <div slot="help">
            {@html $_("config.evse.button-help")}
        </div>
        <div class="is-uppercase has-text-weight-bold is-size-6 mb-3">{$_("config.evse.button")}</div>
        <Switch
            name="buttonenabled"
            label="{formdata.button_enabled.val?$_("enabled"):$_("disabled")}"
            bind:this={formdata.button_enabled.input}
            bind:checked={formdata.button_enabled.val}
            bind:status={formdata.button_enabled.status}
            onChange={()=>setProperty("button_enabled")}
        />
    </Borders>
</div>
```

### 2. src/lib/i18n/en.json

**Changes made:**
Added translation strings for the button control (after line 285):
```json
"button": "Front Button",
"button-help": "<p>Enable or disable the front button on the OpenEVSE unit.</p><br><p>Disabling the button prevents tampering of settings or interrupting a charge, which is useful for outdoor or publicly accessible installations.</p>",
```

## Patch File

A complete patch file is available at: /tmp/gui-button-enable.patch

To apply the patch to the gui-v2 repository:
```bash
cd gui-v2
git apply /tmp/gui-button-enable.patch
```

## Testing

The GUI builds successfully with these changes:
```bash
cd gui-v2
npm install
npm run build
```

Build completed in 6.96s with no errors.
