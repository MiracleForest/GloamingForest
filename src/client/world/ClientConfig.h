// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/config/TFClientConfig.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Config section "client" (F12; rule R13, docs/spec/config.md): TF's client keys owned by F12.
// Client build only; registered in Sky.cpp (GF_REGISTER_CONFIG("client", ClientConfig)).

#pragma once

#ifdef GF_CLIENT

namespace gf::client {

struct ClientConfig {
    int version = 1;
    /// TFClientConfig "screenShakingEffect" (config.twilightforest.screen_shake, default true):
    /// the beanstalk screen shake and every camera::shake.
    bool screenShakingEffect = true;
};

} // namespace gf::client

#endif // GF_CLIENT
