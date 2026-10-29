import React, {useEffect,useState} from "react";
import { createRoot } from "react-dom/client";
import { flushSync } from "react-dom";
import * as Accordion from "@radix-ui/react-accordion";
import * as Tabs from "@radix-ui/react-tabs";
import { ChevronDown } from "lucide-react";
import { iconOnlyActions } from "./icons.jsx";
import { startEditor } from "./editor.js";
import {installControls,syncControls} from './controls.jsx';

// Retain the canvas/controller's DOM nodes while Radix owns navigation and
// disclosure. forceMount preserves input values and listeners in closed groups.
function ControlSlot({ nodes }) {
  return (
    <div
      className="control-stack"
      ref={(el) => {
        if (el) for (const node of nodes) el.appendChild(node);
      }}
    />
  );
}

const panel = document.querySelector(".panel");
const groups = [];
for (const node of [...panel.childNodes]) {
  if (node.nodeName === "H2")
    groups.push({ name: node.textContent, nodes: [] });
  groups.at(-1)?.nodes.push(node);
}
const order = [
  "Palette library",
  "Sprite",
  "File",
  "Hardware",
  "This 8×8 cell",
];
groups.sort((a, b) => order.indexOf(a.name) - order.indexOf(b.name));
const paletteGroup = groups.find((g) => g.name === "Palette library");
groups.splice(groups.indexOf(paletteGroup), 1);
const stage = document.querySelector(".stage");
const preview=document.getElementById("palette-animation-preview");
const previewDock=document.getElementById("tile-previews");
const previewTabs=document.createElement("div");previewTabs.className="tile-preview-tabs row";
previewTabs.setAttribute("role","group");previewTabs.setAttribute("aria-label","Tile preview view");
previewDock.dataset.preview="palette";
for(const [value,label] of [["repeat","Repeating preview"],["palette","Palette preview"]]) {
  const button=document.createElement("button");button.type="button";button.textContent=label;
  button.classList.toggle("on",value==="palette");button.setAttribute("aria-pressed",String(value==="palette"));
  button.onclick=()=>{previewDock.dataset.preview=value;for(const other of previewTabs.children) {
    other.classList.toggle("on",other===button);other.setAttribute("aria-pressed",String(other===button));
  }syncControls();};previewTabs.append(button);
}
previewDock.prepend(previewTabs);
const previewBody=document.createElement("div");previewBody.className="palette-preview-body";
const previewEditor=document.createElement("div");previewEditor.className="palette-preview-editor";
const previewCanvas=document.getElementById("palette-animation-view");
for(const node of [...preview.children])if(node!==preview.firstElementChild && node!==previewCanvas)previewEditor.append(node);
previewBody.append(previewCanvas,previewEditor);preview.append(previewBody);
const art = document.createElement("div");
art.id = "art-workspace";
art.append(...stage.childNodes);
stage.append(art);
const paletteWorkspace = document.createElement("section");
paletteWorkspace.id = "palette-workspace";
paletteWorkspace.hidden = true;
const title = document.createElement("h1");
title.textContent = "Palettes";
paletteWorkspace.append(title);
const description = document.createElement("p");
description.className = "selection-note";
description.textContent =
  "Create and edit four-color palettes here. Painting modes select palettes from the shared library.";
paletteWorkspace.append(description);
paletteWorkspace.append(...paletteGroup.nodes);
stage.append(paletteWorkspace);
stage.prepend(document.getElementById("brush-settings"));
panel.replaceChildren();
flushSync(() =>
  createRoot(panel).render(
    <Accordion.Root
      type="multiple"
      defaultValue={["Sprite"]}
      className="inspector"
    >
      {groups.map((group) => (
        <Accordion.Item
          key={group.name}
          value={group.name}
          className="inspector-card"
        >
          <Accordion.Header className="inspector-heading">
            <Accordion.Trigger className="inspector-trigger">
              {group.name === "Sprite"
                ? "Canvas & animation"
                : group.name === "File"
                ? "Import & export"
                : group.name}
              <ChevronDown size={16} strokeWidth={2} className="inspector-chevron" aria-hidden="true" />
            </Accordion.Trigger>
          </Accordion.Header>
          <Accordion.Content forceMount className="inspector-content">
            <ControlSlot nodes={group.nodes} />
          </Accordion.Content>
        </Accordion.Item>
      ))}
    </Accordion.Root>
  )
);

const modes = document.getElementById("workspace-modes");
function WorkspaceTabs({children}) {
  const [mode,setMode]=useState("sprite");
  useEffect(()=>{
    const change=e=>setMode(e.detail);
    window.addEventListener("workspace-mode-change",change);
    return ()=>window.removeEventListener("workspace-mode-change",change);
  },[]);
  return <Tabs.Root value={mode} onValueChange={setMode} activationMode="manual">{children}</Tabs.Root>;
}
flushSync(() =>
  createRoot(modes).render(
    <WorkspaceTabs>
      <Tabs.List aria-label="Workspace mode" className="mode-list">
        {["sprite", "tile", "scene", "palette"].map((mode) => (
          <Tabs.Trigger key={mode} value={mode} data-mode={mode}>
            {mode === "sprite"
              ? "Sprites"
              : mode === "tile"
              ? "Tiles"
              : mode === "scene"
              ? "Scenes"
              : "Palettes"}
          </Tabs.Trigger>
        ))}
      </Tabs.List>
      {["sprite", "tile", "scene", "palette"].map((mode) => (
        <Tabs.Content value={mode} className="mode-description">
          {mode === "scene"
            ? "Compose linked tile scenes"
            : mode === "tile"
            ? "Design reusable tile art"
            : mode === "palette"
            ? "Manage your color library"
            : "Draw and animate sprite sets"}
        </Tabs.Content>
      ))}
    </WorkspaceTabs>
  )
);
document
  .getElementById("brush-settings")
  .appendChild(document.getElementById("swatches"));
const actions = document.getElementById("project-actions");
panel.id="inspector-panel";panel.setAttribute("aria-label","Inspector");
const inspectorToggle=document.createElement("button");
inspectorToggle.id="toggle-inspector";inspectorToggle.type="button";
inspectorToggle.textContent="Inspector";inspectorToggle.setAttribute("aria-controls",panel.id);
inspectorToggle.setAttribute("aria-expanded","false");
actions.append(inspectorToggle);
const backdrop=document.createElement("div");backdrop.id="inspector-backdrop";backdrop.hidden=true;document.body.append(backdrop);
function setInspector(open) {
  document.querySelector(".app").dataset.inspectorOpen=String(open);
  inspectorToggle.setAttribute("aria-expanded",String(open));
  backdrop.hidden=!open;inspectorToggle.classList.toggle("on",open);
  syncControls();
  if(open)requestAnimationFrame(()=>panel.querySelector(".inspector-trigger")?.focus({preventScroll:true}));
  else document.getElementById("toggle-inspector-control")?.focus({preventScroll:true});
}
inspectorToggle.onclick=()=>setInspector(inspectorToggle.getAttribute("aria-expanded")!=="true");
backdrop.onclick=()=>setInspector(false);
window.addEventListener("keydown",e=>{if(e.key==="Escape" && inspectorToggle.getAttribute("aria-expanded")==="true")setInspector(false);});
matchMedia("(max-width: 560px)").addEventListener("change",()=>setInspector(false));
for (const id of ["file-save-now", "file-save"])
  actions.appendChild(document.getElementById(id));
document.getElementById("file-save-now").textContent = "Save project";
document.getElementById("file-save").textContent = "Download backup";
for (const id of ["asset-list", "asset-name"])
  document
    .getElementById(id)
    .setAttribute(
      "aria-label",
      id === "asset-list" ? "Current asset" : "Asset name"
    );
for (const [id, label] of Object.entries(iconOnlyActions)) {
  const button = document.getElementById(id);
  button.classList.add("icon-button");
  button.title = label;
  button.setAttribute("aria-label", label);
}
startEditor();
installControls();
