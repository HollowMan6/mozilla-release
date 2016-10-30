/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! CSS Multi-column layout http://dev.w3.org/csswg/css-multicol/

#![deny(unsafe_code)]

use app_units::Au;
use block::BlockFlow;
use context::{LayoutContext, SharedLayoutContext};
use display_list_builder::DisplayListBuildState;
use euclid::Point2D;
use euclid::Size2D;
use floats::FloatKind;
use flow::{Flow, FlowClass, OpaqueFlow, mut_base, FragmentationContext};
use flow_ref::{self, FlowRef};
use fragment::{Fragment, FragmentBorderBoxIterator, Overflow};
use gfx::display_list::StackingContext;
use gfx_traits::ScrollRootId;
use gfx_traits::print_tree::PrintTree;
use std::cmp::{min, max};
use std::fmt;
use std::sync::Arc;
use style::context::{StyleContext, SharedStyleContext};
use style::logical_geometry::LogicalSize;
use style::properties::ServoComputedValues;
use style::values::computed::{LengthOrPercentageOrAuto, LengthOrPercentageOrNone};

pub struct MulticolFlow {
    pub block_flow: BlockFlow,

    /// Length between the inline-start edge of a column and that of the next.
    /// That is, the used column-width + used column-gap.
    pub column_pitch: Au,
}

pub struct MulticolColumnFlow {
    pub block_flow: BlockFlow,
}

impl MulticolFlow {
    pub fn from_fragment(fragment: Fragment, float_kind: Option<FloatKind>) -> MulticolFlow {
        MulticolFlow {
            block_flow: BlockFlow::from_fragment_and_float_kind(fragment, float_kind),
            column_pitch: Au(0),
        }
    }
}

impl MulticolColumnFlow {
    pub fn from_fragment(fragment: Fragment) -> MulticolColumnFlow {
        MulticolColumnFlow {
            block_flow: BlockFlow::from_fragment(fragment),
        }
    }
}

impl Flow for MulticolFlow {
    fn class(&self) -> FlowClass {
        FlowClass::Multicol
    }

    fn as_mut_block(&mut self) -> &mut BlockFlow {
        &mut self.block_flow
    }

    fn as_block(&self) -> &BlockFlow {
        &self.block_flow
    }

    fn as_mut_multicol(&mut self) -> &mut MulticolFlow {
        self
    }

    fn bubble_inline_sizes(&mut self) {
        // FIXME(SimonSapin) http://dev.w3.org/csswg/css-sizing/#multicol-intrinsic
        self.block_flow.bubble_inline_sizes();
    }

    fn assign_inline_sizes(&mut self, shared_context: &SharedStyleContext) {
        debug!("assign_inline_sizes({}): assigning inline_size for flow", "multicol");
        self.block_flow.compute_inline_sizes(shared_context);

        // Move in from the inline-start border edge.
        let inline_start_content_edge = self.block_flow.fragment.border_box.start.i +
            self.block_flow.fragment.border_padding.inline_start;

        // Distance from the inline-end margin edge to the inline-end content edge.
        let inline_end_content_edge =
            self.block_flow.fragment.margin.inline_end +
            self.block_flow.fragment.border_padding.inline_end;

        self.block_flow.assign_inline_sizes(shared_context);
        let padding_and_borders = self.block_flow.fragment.border_padding.inline_start_end();
        let content_inline_size =
            self.block_flow.fragment.border_box.size.inline - padding_and_borders;
        let column_width;
        {
            let column_style = self.block_flow.fragment.style.get_column();

            // `None` is 'normal': "UA-specified length. A value of 1em is suggested."
            let column_gap = column_style.column_gap.0.unwrap_or_else(||
                self.block_flow.fragment.style.get_font().font_size);
            let mut column_count;
            if let Some(column_width) = column_style.column_width.0 {
                column_count =
                    max(1, (content_inline_size + column_gap).0 / (column_width + column_gap).0);
                if let Some(specified_column_count) = column_style.column_count.0 {
                    column_count = min(column_count, specified_column_count as i32);
                }
            } else {
                column_count = column_style.column_count.0.unwrap() as i32;
            }
            column_width =
                max(Au(0), (content_inline_size + column_gap) / column_count - column_gap);
            self.column_pitch = column_width + column_gap;
        }

        self.block_flow.fragment.border_box.size.inline = content_inline_size + padding_and_borders;

        self.block_flow.propagate_assigned_inline_size_to_children(
            shared_context, inline_start_content_edge, inline_end_content_edge, column_width,
            |_, _, _, _, _, _| {});
    }

    fn assign_block_size<'a>(&mut self, ctx: &'a LayoutContext<'a>) {
        debug!("assign_block_size: assigning block_size for multicol");

        let fragmentation_context = Some(FragmentationContext {
            this_fragment_is_empty: true,
            available_block_size: {
                let style = &self.block_flow.fragment.style;
                if let LengthOrPercentageOrAuto::Length(length) = style.content_block_size() {
                    length
                } else if let LengthOrPercentageOrNone::Length(length) = style.max_block_size() {
                    length
                } else {
                    // FIXME: do column balancing instead
                    // FIXME: (until column balancing) substract margins/borders/padding
                    LogicalSize::from_physical(
                        self.block_flow.base.writing_mode,
                        ctx.shared_context().viewport_size,
                    ).block
                }
            }
        });

        // Before layout, everything is in a single "column"
        assert!(self.block_flow.base.children.len() == 1);
        let mut column = self.block_flow.base.children.pop_front().unwrap();

        // Pretend there is no children for this:
        self.block_flow.assign_block_size(ctx);

        loop {
            let remaining = flow_ref::deref_mut(&mut column).fragment(ctx, fragmentation_context);
            self.block_flow.base.children.push_back(column);
            column = match remaining {
                Some(remaining) => remaining,
                None => break
            };
        }
    }

    fn compute_absolute_position(&mut self, layout_context: &SharedLayoutContext) {
        self.block_flow.compute_absolute_position(layout_context);
        let pitch = LogicalSize::new(self.block_flow.base.writing_mode, self.column_pitch, Au(0));
        let pitch = pitch.to_physical(self.block_flow.base.writing_mode);
        for (i, child) in self.block_flow.base.children.iter_mut().enumerate() {
            let point = &mut mut_base(child).stacking_relative_position;
            *point = *point + Size2D::new(pitch.width * i as i32, pitch.height * i as i32);
        }
    }

    fn update_late_computed_inline_position_if_necessary(&mut self, inline_position: Au) {
        self.block_flow.update_late_computed_inline_position_if_necessary(inline_position)
    }

    fn update_late_computed_block_position_if_necessary(&mut self, block_position: Au) {
        self.block_flow.update_late_computed_block_position_if_necessary(block_position)
    }

    fn build_display_list(&mut self, state: &mut DisplayListBuildState) {
        debug!("build_display_list_multicol");
        self.block_flow.build_display_list(state);
    }

    fn collect_stacking_contexts(&mut self,
                                 parent: &mut StackingContext,
                                 parent_scroll_root_id: ScrollRootId) {
        self.block_flow.collect_stacking_contexts(parent, parent_scroll_root_id);
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
        self.block_flow.iterate_through_fragment_border_boxes(iterator, level, stacking_context_position);
    }

    fn mutate_fragments(&mut self, mutator: &mut FnMut(&mut Fragment)) {
        self.block_flow.mutate_fragments(mutator);
    }

    fn print_extra_flow_children(&self, print_tree: &mut PrintTree) {
        self.block_flow.print_extra_flow_children(print_tree);
    }
}

impl Flow for MulticolColumnFlow {
    fn class(&self) -> FlowClass {
        FlowClass::MulticolColumn
    }

    fn as_mut_block(&mut self) -> &mut BlockFlow {
        &mut self.block_flow
    }

    fn as_block(&self) -> &BlockFlow {
        &self.block_flow
    }

    fn bubble_inline_sizes(&mut self) {
        self.block_flow.bubble_inline_sizes();
    }

    fn assign_inline_sizes(&mut self, shared_context: &SharedStyleContext) {
        debug!("assign_inline_sizes({}): assigning inline_size for flow", "multicol column");
        self.block_flow.assign_inline_sizes(shared_context);
    }

    fn assign_block_size<'a>(&mut self, ctx: &'a LayoutContext<'a>) {
        debug!("assign_block_size: assigning block_size for multicol column");
        self.block_flow.assign_block_size(ctx);
    }

    fn fragment(&mut self, layout_context: &LayoutContext,
                fragmentation_context: Option<FragmentationContext>)
                -> Option<FlowRef> {
        Flow::fragment(&mut self.block_flow, layout_context, fragmentation_context)
    }

    fn compute_absolute_position(&mut self, layout_context: &SharedLayoutContext) {
        self.block_flow.compute_absolute_position(layout_context)
    }

    fn update_late_computed_inline_position_if_necessary(&mut self, inline_position: Au) {
        self.block_flow.update_late_computed_inline_position_if_necessary(inline_position)
    }

    fn update_late_computed_block_position_if_necessary(&mut self, block_position: Au) {
        self.block_flow.update_late_computed_block_position_if_necessary(block_position)
    }

    fn build_display_list(&mut self, state: &mut DisplayListBuildState) {
        debug!("build_display_list_multicol column");
        self.block_flow.build_display_list(state);
    }

    fn collect_stacking_contexts(&mut self,
                                 parent: &mut StackingContext,
                                 parent_scroll_root_id: ScrollRootId) {
        self.block_flow.collect_stacking_contexts(parent, parent_scroll_root_id);
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
        self.block_flow.iterate_through_fragment_border_boxes(iterator, level, stacking_context_position);
    }

    fn mutate_fragments(&mut self, mutator: &mut FnMut(&mut Fragment)) {
        self.block_flow.mutate_fragments(mutator);
    }

    fn print_extra_flow_children(&self, print_tree: &mut PrintTree) {
        self.block_flow.print_extra_flow_children(print_tree);
    }
}

impl fmt::Debug for MulticolFlow {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "MulticolFlow: {:?}", self.block_flow)
    }
}

impl fmt::Debug for MulticolColumnFlow {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "MulticolColumnFlow: {:?}", self.block_flow)
    }
}
