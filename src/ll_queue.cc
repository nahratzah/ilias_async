/*
 * Copyright (c) 2013 Ariane van der Steldt <ariane@stack.nl>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <ilias/ll_queue.h>
#include <cassert>

namespace ilias {
namespace ll_queue_detail {


auto ll_qhead::push_back_(elem& e) noexcept -> void {
  head_.link_back(e);
}

auto ll_qhead::pop_front_() noexcept -> elem* {
  return head_.pop_front().get();
}

auto ll_qhead::push_front_(elem& e) noexcept -> void {
  head_.link_front(e);
}

auto ll_qhead::size() const noexcept -> size_type {
  return head_.size();
}

auto ll_qhead::empty() const noexcept -> bool {
  return head_.empty();
}



}} /* namespace ilias::ll_queue_detail */
