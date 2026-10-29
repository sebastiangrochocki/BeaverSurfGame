import {
  Pencil, PaintBucket, Pipette, SquareDashed, Eraser, Paintbrush,
  Undo2, Redo2, Plus, Copy, CopyPlus, Trash2, ZoomIn, ZoomOut,
  FlipHorizontal2, FlipVertical2, ArrowRight, ArrowLeft, ArrowDown, ArrowUp,
  Play, Square, Save, Download, FolderOpen, FilePlus2, ImageUp, ImageDown,
  Package, BookmarkPlus, Layers, Grid2X2, RotateCw, Check, Stamp, Maximize, SlidersHorizontal,
} from "lucide-react";

// One registry for static and dynamically generated controls in every mode.
// Keep text labels on secondary actions; only established tools are icon-only.
export const iconOnlyActions = {
  "tool-draw": "Brush / stamp tiles",
  "tool-fill": "Fill",
  "tool-pick": "Pick color / tile",
  "tool-select": "Select",
  "tool-recolor": "Recolor",
  erase: "Eraser", undo: "Undo", redo: "Redo",
  "asset-add": "Add asset", "zoom-in": "Zoom in", "zoom-out": "Zoom out",
  "palette-clone": "Clone palette frame", "palette-drop": "Delete palette frame",
};

const actions = {
  "tool-draw": Pencil, "tool-fill": PaintBucket, "tool-pick": Pipette,
  "tool-select": SquareDashed, "tool-recolor": Paintbrush, erase: Eraser,
  undo: Undo2, redo: Redo2, "asset-add": Plus, "asset-dup": Copy,
  "asset-delete": Trash2, "scene-erase": Eraser, "zoom-in": ZoomIn,
  "zoom-out": ZoomOut, "reflect-horizontal": FlipHorizontal2,
  "reflect-vertical": FlipVertical2, "reflect-left-right": ArrowRight,
  "reflect-right-left": ArrowLeft, "reflect-top-bottom": ArrowDown,
  "reflect-bottom-top": ArrowUp, clone: CopyPlus, "blank-frame": Plus,
  "copy-png": Copy, "drop-frame": Trash2, "palette-clone": CopyPlus,
  "palette-drop": Trash2, "pal-add": Plus, "pal-dup": Copy,
  "pal-del": Trash2, "pal-save": BookmarkPlus, "hex-apply": Check,
  "layer-tiles": Grid2X2, "layer-pixels": Layers, "file-new": FilePlus2,
  "file-open": FolderOpen, "file-save-now": Save, "file-save": Download,
  "file-import": ImageUp, "file-export": ImageDown, "file-gbc": Package,
  "fit-view": Maximize, "toggle-inspector": SlidersHorizontal,
};

export function actionIcon(id, text, title) {
  if (id === "play" || id === "palette-play") return text === "Stop" ? Square : Play;
  if (id === "tool-draw" && title === "Stamp tiles") return Stamp;
  if (id?.startsWith("rotate-")) return RotateCw;
  return actions[id];
}
