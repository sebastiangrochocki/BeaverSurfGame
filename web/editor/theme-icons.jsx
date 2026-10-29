import React from "react";
import { ChevronDown, ChevronRight, Check, Minus } from "lucide-react";

// Radix Themes' behavior stays intact. The build redirects its four internal
// glyph exports here so popup/checkbox chrome uses the same family as tools.
const glyph = Icon => React.forwardRef((props, ref) =>
  <Icon size={14} strokeWidth={2} aria-hidden="true" {...props} ref={ref} />
);
export const ChevronDownIcon = glyph(ChevronDown);
export const ThickChevronRightIcon = glyph(ChevronRight);
export const ThickCheckIcon = glyph(Check);
export const ThickDividerHorizontalIcon = glyph(Minus);
