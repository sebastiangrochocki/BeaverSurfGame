import React from "react";
import { createRoot } from "react-dom/client";
import {
  Theme,
  Select,
  Slider,
  Checkbox,
  TextField,
  Button,
  IconButton,
  Tooltip,
  Link,
  AlertDialog,
  Flex,
} from "@radix-ui/themes";
import "@radix-ui/themes/styles.css";
import { actionIcon } from "./icons.jsx";

// The canvas controller retains its data fields. All visible controls are Radix.
const controls = new Map();
let enabled = false;
const event = (source, name) =>
  source.dispatchEvent(new Event(name, { bubbles: true }));
const ThemeRoot = ({ children }) => (
  <Theme
    appearance="dark"
    accentColor="purple"
    grayColor="gray"
    radius="medium"
    scaling="95%"
    className="control-theme"
  >
    {children}
  </Theme>
);

function stateFor(source) {
  const label =
    source.getAttribute("aria-label") ||
    source.title ||
    [...(source.closest("label")?.childNodes || [])]
      .filter(
        (n) =>
          n.nodeType === 3 ||
          (n.nodeName === "SPAN" && !n.classList.contains("radix-control"))
      )
      .map((n) => n.textContent.trim())
      .join(" ")
      .trim() ||
    source.textContent.trim() ||
    source.id;
  return {
    label,
    title: source.title || "",
    value: source.value || "",
    checked: source.checked,
    hidden: source.hidden,
    disabled: source.disabled,
    html: source.innerHTML,
    className: source.className.replace("control-source", "").trim(),
    style: source.getAttribute("style"),
    min: source.min,
    max: source.max,
    step: source.step,
    placeholder: source.placeholder,
    maxLength: source.maxLength,
    type: source.type,
    options:
      source.tagName === "SELECT"
        ? [...source.options].map((o) => ({
            value: o.value,
            label: o.textContent,
            disabled: o.disabled,
          }))
        : [],
    pressed: source.getAttribute("aria-pressed"),
    expanded: source.getAttribute("aria-expanded"),
    controls: source.getAttribute("aria-controls"),
    href: source.getAttribute("href"),
  };
}

function Control({ source, state }) {
  const id = source.id ? source.id + "-control" : undefined;
  const setValue = (value, name = "change") => {
    source.value = value;
    event(source, name);
    syncControls();
  };
  const common = { id, "aria-label": state.label, disabled: state.disabled,
    "aria-expanded": state.expanded || undefined,"aria-controls":state.controls || undefined };
  if (source.tagName === "SELECT")
    return (
      <Select.Root
        value={state.value || "__empty__"}
        onValueChange={(v) => setValue(v === "__empty__" ? "" : v)}
      >
        <Select.Trigger
          {...common}
          className="workspace-select"
          placeholder="Choose…"
        />
        <Select.Content position="popper" className="workspace-select-content">
          {state.options.map((o, i) => (
            <Select.Item
              key={o.value + ":" + i}
              value={o.value || "__empty__"}
              disabled={o.disabled}
            >
              {o.label}
            </Select.Item>
          ))}
        </Select.Content>
      </Select.Root>
    );
  if (source.type === "checkbox")
    return (
      <Checkbox
        {...common}
        checked={state.checked}
        onCheckedChange={(checked) => {
          source.checked = checked === true;
          event(source, "change");
          event(source, "input");
          source.dispatchEvent(new Event("workspace-redraw"));
          syncControls();
        }}
      />
    );
  if (source.type === "range")
    return (
      <Slider
        {...common}
        min={0}
        max={31}
        step={1}
        value={[Math.round((Number(state.value) * 31) / 255)]}
        onValueChange={([v]) =>
          setValue(String(Math.round((v * 255) / 31)), "input")
        }
      />
    );
  if (source.tagName === "INPUT")
    return (
      <TextField.Root
        {...common}
        type={state.type}
        value={state.value}
        min={state.min || undefined}
        max={state.max || undefined}
        step={state.step || undefined}
        maxLength={state.maxLength > 0 ? state.maxLength : undefined}
        placeholder={state.placeholder}
        onChange={(e) => setValue(e.target.value, "input")}
        onBlur={() => {
          event(source, "change");
          syncControls();
        }}
        onKeyDown={(e) => {
          if (e.key === "Enter") {
            event(source, "change");
            source.dispatchEvent(
              new KeyboardEvent("keydown", { key: e.key, bubbles: true })
            );
            syncControls();
          }
        }}
      />
    );
  if (source.tagName === "A")
    return (
      <Link href={state.href} className={state.className} highContrast>
        {source.textContent}
      </Link>
    );
  const Component = source.classList.contains("icon-button")
    ? IconButton
    : Button;
  const selected = state.className.split(/\s+/).includes("on");
  const Icon = actionIcon(source.id, source.textContent.trim(), source.title);
  const button = (
    <Component
      {...common}
      type="button"
      variant={selected ? "solid" : "surface"}
      color={
        source.id === "asset-delete" || source.id === "pal-del"
          ? "red"
          : selected
          ? "purple"
          : "gray"
      }
      className={state.className}
      style={
        source.style.background
          ? { background: source.style.background }
          : undefined
      }
      aria-pressed={state.pressed || undefined}
      onClick={() => {
        source.click();
        syncControls();
      }}
    >
      {Icon && <Icon size={18} strokeWidth={2} className="editor-action-icon" aria-hidden="true" />}
      {(!Icon || !source.classList.contains("icon-button")) && <span dangerouslySetInnerHTML={{ __html: state.html }} />}
    </Component>
  );
  return source.title ? (
    <Tooltip content={source.title}>{button}</Tooltip>
  ) : (
    button
  );
}

export function syncControls() {
  if (!enabled) return;
  for (const [source, entry] of controls)
    if (!source.isConnected) {
      controls.delete(source);
      queueMicrotask(() => entry.root.unmount());
    }
  for (const source of document.querySelectorAll(
    "select,input:not([type=file]),button,nav a"
  )) {
    if (
      source.closest(".radix-themes") ||
      source.closest("#workspace-modes") ||
      source.classList.contains("inspector-trigger")
    )
      continue;
    let entry = controls.get(source);
    if (!entry) {
      const mount = document.createElement("span");
      mount.className = "radix-control";
      mount.dataset.control = source.id || source.parentElement?.id || "action";
      source.before(mount);
      source.classList.add("control-source");
      source.tabIndex = -1;
      source.setAttribute("aria-hidden", "true");
      entry = { mount, root: createRoot(mount), signature: null };
      controls.set(source, entry);
    }
    const state = stateFor(source),
      signature = JSON.stringify(state);
    entry.mount.hidden = state.hidden;
    if (entry.signature === signature) continue;
    entry.signature = signature;
    entry.root.render(
      <ThemeRoot>
        <Control source={source} state={state} />
      </ThemeRoot>
    );
  }
}
export function installControls() {
  enabled = true;
  syncControls();
}

function messageDialog(message, confirmation) {
  return new Promise((resolve) => {
    const host = document.createElement("div");
    document.body.append(host);
    const root = createRoot(host);
    let finished = false;
    const finish = (answer) => {
      if (finished) return;
      finished = true;
      queueMicrotask(() => {
        root.unmount();
        host.remove();
        resolve(answer);
      });
    };
    root.render(
      <ThemeRoot>
        <AlertDialog.Root
          defaultOpen
          onOpenChange={(open) => {
            if (!open) finish(false);
          }}
        >
          <AlertDialog.Content maxWidth="440px">
            <AlertDialog.Title>
              {confirmation ? "New project" : "Art workspace"}
            </AlertDialog.Title>
            <AlertDialog.Description>{message}</AlertDialog.Description>
            <Flex justify="end" gap="3" mt="5">
              {confirmation && (
                <Button
                  variant="soft"
                  color="gray"
                  onClick={() => finish(false)}
                >
                  Cancel
                </Button>
              )}
              <Button onClick={() => finish(true)}>
                {confirmation ? "Create project" : "OK"}
              </Button>
            </Flex>
          </AlertDialog.Content>
        </AlertDialog.Root>
      </ThemeRoot>
    );
  });
}
export const notify = (message) => messageDialog(message, false);
export const confirmAction = (message) => messageDialog(message, true);
