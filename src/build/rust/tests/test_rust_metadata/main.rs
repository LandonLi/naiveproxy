// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

fn main() {
    lib::print_foo_bar();
    println!("{} from re-exported function", lib::say_foo_directly());
}
