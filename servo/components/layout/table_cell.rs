/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! CSS table formatting contexts.

#![deny(unsafe_code)]

use app_units::Au;
use block::{BlockFlow, ISizeAndMarginsComputer, MarginsMayCollapseFlag};
use context::LayoutContext;
use cssparser::Color;
use display_list_builder::{BlockFlowDisplayListBuilding, BorderPaintingMode, DisplayListBuildState};
use euclid::{Point2D, Rect, SideOffsets2D, Size2D};
use flow::{self, Flow, FlowClass, OpaqueFlow};
use fragment::{Fragment, FragmentBorderBoxIterator, Overflow};
use gfx::display_list::StackingContext;
use gfx_traits::StackingContextId;
use gfx_traits::print_tree::PrintTree;
use layout_debug;
use model::MaybeAuto;
use script_layout_interface::restyle_damage::REFLOW;
use script_layout_interface::wrapper_traits::ThreadSafeLayoutNode;
use std::fmt;
use std::sync::Arc;
use style::computed_values::{border_collapse, border_top_style, vertical_align};
use style::context::SharedStyleContext;
use style::logical_geometry::{LogicalMargin, LogicalRect, LogicalSize, WritingMode};
use style::properties::ServoComputedValues;
use table::InternalTable;
use table_row::{CollapsedBorder, CollapsedBorderProvenance};

/// A table formatting context.
#[derive(RustcEncodable)]
pub struct TableCellFlow {
    /// Data common to all block flows.
    pub block_flow: BlockFlow,

    /// Border collapse information for the cell.
    pub collapsed_borders: CollapsedBordersForCell,

    /// The column span of this cell.
    pub column_span: u32,

    /// Whether this cell is visible. If false, the value of `empty-cells` means that we must not
    /// display this cell.
    pub visible: bool,
}

impl TableCellFlow {
    pub fn from_node_fragment_and_visibility_flag<N: ThreadSafeLayoutNode>(
            node: &N, fragment: Fragment, visible: bool) -> TableCellFlow {
        TableCellFlow {
            block_flow: BlockFlow::from_fragment(fragment, None),
            collapsed_borders: CollapsedBordersForCell::new(),
            column_span: node.get_colspan(),
            visible: visible,
        }
    }

    pub fn fragment(&mut self) -> &Fragment {
        &self.block_flow.fragment
    }

    pub fn mut_fragment(&mut self) -> &mut Fragment {
        &mut self.block_flow.fragment
    }

    /// Assign block-size for table-cell flow.
    ///
    /// inline(always) because this is only ever called by in-order or non-in-order top-level
    /// methods.
    #[inline(always)]
    fn assign_block_size_table_cell_base<'a>(&mut self, layout_context: &'a LayoutContext<'a>) {
        let remaining = self.block_flow.assign_block_size_block_base(
            layout_context,
            None,
            MarginsMayCollapseFlag::MarginsMayNotCollapse);
        debug_assert!(remaining.is_none());
    }

    /// Position this cell's children according to vertical-align.
    pub fn valign_children(&mut self) {
        // Note to the reader: this code has been tested with negative margins.
        // We end up with a "end" that's before the "start," but the math still works out.
        let first_start = flow::base(self).children.front().map(|kid| {
            let kid_base = flow::base(kid);
            flow::base(kid).position.start.b
                - kid_base.collapsible_margins.block_start_margin_for_noncollapsible_context()
        });
        if let Some(mut first_start) = first_start {
            let mut last_end = first_start;
            for kid in flow::base(self).children.iter() {
                let kid_base = flow::base(kid);
                let start = kid_base.position.start.b
                    - kid_base.collapsible_margins.block_start_margin_for_noncollapsible_context();
                let end = kid_base.position.start.b + kid_base.position.size.block
                    + kid_base.collapsible_margins.block_end_margin_for_noncollapsible_context();
                if start < first_start {
                    first_start = start;
                }
                if end > last_end {
                    last_end = end;
                }
            }
            let kids_size = last_end - first_start;
            let self_size = flow::base(self).position.size.block -
                self.block_flow.fragment.border_padding.block_start_end();
            let kids_self_gap = self_size - kids_size;

            // This offset should also account for vertical_align::T::baseline.
            // Need max cell ascent from the first row of this cell.
            let offset = match self.block_flow.fragment.style().get_box().vertical_align {
                vertical_align::T::middle => kids_self_gap / 2,
                vertical_align::T::bottom => kids_self_gap,
                _ => Au(0),
            };
            if offset != Au(0) {
                for kid in flow::mut_base(self).children.iter_mut() {
                    flow::mut_base(kid).position.start.b = flow::mut_base(kid).position.start.b + offset;
                }
            }
        }
    }
}

impl Flow for TableCellFlow {
    fn class(&self) -> FlowClass {
        FlowClass::TableCell
    }

    fn as_mut_table_cell(&mut self) -> &mut TableCellFlow {
        self
    }

    fn as_table_cell(&self) -> &TableCellFlow {
        self
    }

    fn as_mut_block(&mut self) -> &mut BlockFlow {
        &mut self.block_flow
    }

    fn as_block(&self) -> &BlockFlow {
        &self.block_flow
    }

    /// Minimum/preferred inline-sizes set by this function are used in automatic table layout
    /// calculation.
    fn bubble_inline_sizes(&mut self) {
        let _scope = layout_debug_scope!("table_cell::bubble_inline_sizes {:x}",
                                         self.block_flow.base.debug_id());

        self.block_flow.bubble_inline_sizes_for_block(true);
        let specified_inline_size = MaybeAuto::from_style(self.block_flow
                                                              .fragment
                                                              .style()
                                                              .content_inline_size(),
                                                          Au(0)).specified_or_zero();
        if self.block_flow.base.intrinsic_inline_sizes.minimum_inline_size <
                specified_inline_size {
            self.block_flow.base.intrinsic_inline_sizes.minimum_inline_size = specified_inline_size
        }
        if self.block_flow.base.intrinsic_inline_sizes.preferred_inline_size <
                self.block_flow.base.intrinsic_inline_sizes.minimum_inline_size {
            self.block_flow.base.intrinsic_inline_sizes.preferred_inline_size =
                self.block_flow.base.intrinsic_inline_sizes.minimum_inline_size;
        }
    }

    /// Recursively (top-down) determines the actual inline-size of child contexts and fragments.
    /// When called on this context, the context has had its inline-size set by the parent table
    /// row.
    fn assign_inline_sizes(&mut self, shared_context: &SharedStyleContext) {
        let _scope = layout_debug_scope!("table_cell::assign_inline_sizes {:x}",
                                            self.block_flow.base.debug_id());
        debug!("assign_inline_sizes({}): assigning inline_size for flow", "table_cell");

        // The position was set to the column inline-size by the parent flow, table row flow.
        let containing_block_inline_size = self.block_flow.base.block_container_inline_size;

        let inline_size_computer = InternalTable {
            border_collapse: self.block_flow.fragment.style.get_inheritedtable().border_collapse,
        };
        inline_size_computer.compute_used_inline_size(&mut self.block_flow,
                                                      shared_context,
                                                      containing_block_inline_size);

        let inline_start_content_edge =
            self.block_flow.fragment.border_box.start.i +
            self.block_flow.fragment.border_padding.inline_start;
        let inline_end_content_edge =
            self.block_flow.base.block_container_inline_size -
            self.block_flow.fragment.border_padding.inline_start_end() -
            self.block_flow.fragment.border_box.size.inline;
        let padding_and_borders = self.block_flow.fragment.border_padding.inline_start_end();
        let content_inline_size =
            self.block_flow.fragment.border_box.size.inline - padding_and_borders;

        self.block_flow.propagate_assigned_inline_size_to_children(shared_context,
                                                                   inline_start_content_edge,
                                                                   inline_end_content_edge,
                                                                   content_inline_size,
                                                                   |_, _, _, _, _, _| {});
    }

    fn assign_block_size<'a>(&mut self, layout_context: &'a LayoutContext<'a>) {
        debug!("assign_block_size: assigning block_size for table_cell");
        self.assign_block_size_table_cell_base(layout_context);
    }

    fn compute_absolute_position(&mut self, layout_context: &LayoutContext) {
        self.block_flow.compute_absolute_position(layout_context)
    }

    fn update_late_computed_inline_position_if_necessary(&mut self, inline_position: Au) {
        self.block_flow.update_late_computed_inline_position_if_necessary(inline_position)
    }

    fn update_late_computed_block_position_if_necessary(&mut self, block_position: Au) {
        self.block_flow.update_late_computed_block_position_if_necessary(block_position)
    }

    fn build_display_list(&mut self, state: &mut DisplayListBuildState) {
        if !self.visible {
            return
        }

        let border_painting_mode = match self.block_flow
                                             .fragment
                                             .style
                                             .get_inheritedtable()
                                             .border_collapse {
            border_collapse::T::separate => BorderPaintingMode::Separate,
            border_collapse::T::collapse => BorderPaintingMode::Collapse(&self.collapsed_borders),
        };

        self.block_flow.build_display_list_for_block(state, border_painting_mode)
    }

    fn collect_stacking_contexts(&mut self,
                                 parent_id: StackingContextId,
                                 contexts: &mut Vec<Box<StackingContext>>)
                                 -> StackingContextId {
        self.block_flow.collect_stacking_contexts(parent_id, contexts)
    }

    fn repair_style(&mut self, new_style: &Arc<ServoComputedValues>) {
        self.block_flow.repair_style(new_style)
    }

    fn compute_overflow(&self) -> Overflow {
        self.block_flow.compute_overflow()
    }

    fn generated_containing_block_size(&self, flow: OpaqueFlow) -> LogicalSize<Au> {
        self.block_flow.generated_containing_block_size(flow)
    }

    fn iterate_through_fragment_border_boxes(&self,
                                             iterator: &mut FragmentBorderBoxIterator,
                                             level: i32,
                                             stacking_context_position: &Point2D<Au>) {
        self.block_flow.iterate_through_fragment_border_boxes(iterator, level, stacking_context_position)
    }

    fn mutate_fragments(&mut self, mutator: &mut FnMut(&mut Fragment)) {
        self.block_flow.mutate_fragments(mutator)
    }

    fn print_extra_flow_children(&self, print_tree: &mut PrintTree) {
        self.block_flow.print_extra_flow_children(print_tree);
    }
}

impl fmt::Debug for TableCellFlow {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "TableCellFlow: {:?}", self.block_flow)
    }
}

#[derive(Copy, Clone, Debug, RustcEncodable)]
pub struct CollapsedBordersForCell {
    pub inline_start_border: CollapsedBorder,
    pub inline_end_border: CollapsedBorder,
    pub block_start_border: CollapsedBorder,
    pub block_end_border: CollapsedBorder,
    pub inline_start_width: Au,
    pub inline_end_width: Au,
    pub block_start_width: Au,
    pub block_end_width: Au,
}

impl CollapsedBordersForCell {
    fn new() -> CollapsedBordersForCell {
        CollapsedBordersForCell {
            inline_start_border: CollapsedBorder::new(),
            inline_end_border: CollapsedBorder::new(),
            block_start_border: CollapsedBorder::new(),
            block_end_border: CollapsedBorder::new(),
            inline_start_width: Au(0),
            inline_end_width: Au(0),
            block_start_width: Au(0),
            block_end_width: Au(0),
        }
    }

    fn should_paint_inline_start_border(&self) -> bool {
        self.inline_start_border.provenance != CollapsedBorderProvenance::FromPreviousTableCell
    }

    fn should_paint_inline_end_border(&self) -> bool {
        self.inline_end_border.provenance != CollapsedBorderProvenance::FromNextTableCell
    }

    fn should_paint_block_start_border(&self) -> bool {
        self.block_start_border.provenance != CollapsedBorderProvenance::FromPreviousTableCell
    }

    fn should_paint_block_end_border(&self) -> bool {
        self.block_end_border.provenance != CollapsedBorderProvenance::FromNextTableCell
    }

    pub fn adjust_border_widths_for_painting(&self, border_widths: &mut LogicalMargin<Au>) {
        border_widths.inline_start = if !self.should_paint_inline_start_border() {
            Au(0)
        } else {
            self.inline_start_border.width
        };
        border_widths.inline_end = if !self.should_paint_inline_end_border() {
            Au(0)
        } else {
            self.inline_end_border.width
        };
        border_widths.block_start = if !self.should_paint_block_start_border() {
            Au(0)
        } else {
            self.block_start_border.width
        };
        border_widths.block_end = if !self.should_paint_block_end_border() {
            Au(0)
        } else {
            self.block_end_border.width
        }
    }

    pub fn adjust_border_bounds_for_painting(&self,
                                             border_bounds: &mut Rect<Au>,
                                             writing_mode: WritingMode) {
        let inline_start_divisor = if self.should_paint_inline_start_border() {
            2
        } else {
            -2
        };
        let inline_start_offset = self.inline_start_width / 2 + self.inline_start_border.width /
            inline_start_divisor;
        let inline_end_divisor = if self.should_paint_inline_end_border() {
            2
        } else {
            -2
        };
        let inline_end_offset = self.inline_end_width / 2 + self.inline_end_border.width /
            inline_end_divisor;
        let block_start_divisor = if self.should_paint_block_start_border() {
            2
        } else {
            -2
        };
        let block_start_offset = self.block_start_width / 2 + self.block_start_border.width /
            block_start_divisor;
        let block_end_divisor = if self.should_paint_block_end_border() {
            2
        } else {
            -2
        };
        let block_end_offset = self.block_end_width / 2 + self.block_end_border.width /
            block_end_divisor;

        // FIXME(pcwalton): Get the real container size.
        let mut logical_bounds =
            LogicalRect::from_physical(writing_mode, *border_bounds, Size2D::new(Au(0), Au(0)));
        logical_bounds.start.i = logical_bounds.start.i - inline_start_offset;
        logical_bounds.start.b = logical_bounds.start.b - block_start_offset;
        logical_bounds.size.inline = logical_bounds.size.inline + inline_start_offset +
            inline_end_offset;
        logical_bounds.size.block = logical_bounds.size.block + block_start_offset +
            block_end_offset;
        *border_bounds = logical_bounds.to_physical(writing_mode, Size2D::new(Au(0), Au(0)))
    }

    pub fn adjust_border_colors_and_styles_for_painting(
            &self,
            border_colors: &mut SideOffsets2D<Color>,
            border_styles: &mut SideOffsets2D<border_top_style::T>,
            writing_mode: WritingMode) {
        let logical_border_colors = LogicalMargin::new(writing_mode,
                                                       self.block_start_border.color,
                                                       self.inline_end_border.color,
                                                       self.block_end_border.color,
                                                       self.inline_start_border.color);
        *border_colors = logical_border_colors.to_physical(writing_mode);

        let logical_border_styles = LogicalMargin::new(writing_mode,
                                                       self.block_start_border.style,
                                                       self.inline_end_border.style,
                                                       self.block_end_border.style,
                                                       self.inline_start_border.style);
        *border_styles = logical_border_styles.to_physical(writing_mode);
    }
}
