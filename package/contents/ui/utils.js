.pragma library

// Keeps a ListModel in step with a JS array without recreating delegates,
// so fences don't flicker or lose state when something else changes.
function syncModel(model, items, key) {
    // Hash membership once instead of scanning the incoming array for every
    // existing row. This makes the common unchanged/append/remove refresh O(n).
    const wantedKeys = new Set();
    for (let i = 0; i < items.length; ++i) {
        wantedKeys.add(items[i][key]);
    }

    for (let i = model.count - 1; i >= 0; --i) {
        if (!wantedKeys.has(model.get(i)[key])) {
            model.remove(i);
        }
    }

    // Keep key lookups constant-time while preserving delegates for rows that
    // still exist. Only positions crossed by an insert/move need reindexing.
    const indexByKey = new Map();
    for (let i = 0; i < model.count; ++i) {
        indexByKey.set(model.get(i)[key], i);
    }

    for (let i = 0; i < items.length; ++i) {
        const item = items[i];
        const itemKey = item[key];
        const found = indexByKey.get(itemKey);

        if (found === undefined) {
            model.insert(i, item);
            for (let j = i; j < model.count; ++j) {
                indexByKey.set(model.get(j)[key], j);
            }
        } else if (found !== i) {
            model.move(found, i, 1);
            for (let j = i; j <= found; ++j) {
                indexByKey.set(model.get(j)[key], j);
            }
        }

        const current = model.get(i);
        for (const role in item) {
            if (current[role] !== item[role]) {
                model.setProperty(i, role, item[role]);
            }
        }
    }
}

function fenceRow(f) {
    return {
        fid: f.id,
        title: f.title,
        fx: f.x, fy: f.y, fw: f.w, fh: f.h,
        collapsed: f.collapsed,
        rules: f.rules,
        timeRule: f.timeRule,
        portal: f.portal,
        group: f.group,
        tabsJson: JSON.stringify(f.tabs),
        color: f.color,
        fenceOpacity: f.opacity,
        blur: f.blur,
        view: f.view,
        fenceIconSize: f.iconSize,
        screen: f.screen,
        orphan: f.orphan,
        activity: f.activity,
        page: f.page
    };
}

function fileRow(e) {
    return {
        name: e.name,
        path: e.path,
        label: e.label,
        url: e.url,
        icon: e.icon,
        fallbackIcon: e.fallbackIcon,
        image: e.image,
        mime: e.mime,
        isDir: e.isDir,
        size: e.size,
        modified: e.modified ? e.modified.getTime() : 0,
        canThumb: e.canThumb,
        hasPos: e.hasPos === true,
        posPage: e.page || 0,
        col: e.col || 0,
        row: e.row || 0
    };
}

function intersects(a, b) {
    return a.x < b.x + b.width && b.x < a.x + a.width
        && a.y < b.y + b.height && b.y < a.y + a.height;
}

function humanSize(bytes) {
    if (bytes < 1024) return bytes + " B";
    const units = ["KB", "MB", "GB", "TB"];
    let v = bytes / 1024, i = 0;
    while (v >= 1024 && i < units.length - 1) { v /= 1024; ++i; }
    return (v >= 10 ? Math.round(v) : v.toFixed(1)) + " " + units[i];
}

// Hexagon cell centres for the honeycomb view
function honeycomb(count, width, cell) {
    const w = cell, h = cell * 0.87;
    const perRow = Math.max(1, Math.floor((width - w / 2) / w));
    const out = [];
    for (let i = 0; i < count; ++i) {
        const row = Math.floor(i / perRow), col = i % perRow;
        out.push({ x: col * w + (row % 2 ? w / 2 : 0), y: row * h });
    }
    return out;
}

function honeycombHeight(count, width, cell) {
    const w = cell, h = cell * 0.87;
    const perRow = Math.max(1, Math.floor((width - w / 2) / w));
    return Math.ceil(count / perRow) * h + cell * 0.2;
}
