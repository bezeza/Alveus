.pragma library

// Keeps a ListModel in step with a JS array without recreating delegates,
// so fences don't flicker or lose state when something else changes.
function syncModel(model, items, key) {
    const keys = items.map(function (i) { return i[key]; });
    for (let i = model.count - 1; i >= 0; --i) {
        if (keys.indexOf(model.get(i)[key]) === -1) {
            model.remove(i);
        }
    }
    for (let i = 0; i < items.length; ++i) {
        const item = items[i];
        let found = -1;
        for (let j = i; j < model.count; ++j) {
            if (model.get(j)[key] === item[key]) {
                found = j;
                break;
            }
        }
        if (found === -1) {
            model.insert(i, item);
            continue;
        }
        if (found !== i) {
            model.move(found, i, 1);
        }
        const cur = model.get(i);
        for (const k in item) {
            if (cur[k] !== item[k]) {
                model.setProperty(i, k, item[k]);
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
