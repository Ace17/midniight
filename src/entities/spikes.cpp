// Copyright (C) 2018 - Sebastien Alaiwan
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

#include "base/scene.h"
#include "base/util.h"

#include "gameplay/collision_groups.h"
#include "gameplay/entity.h"
#include "gameplay/models.h"

struct Spikes : Entity
{
  Spikes()
  {
    size = Size(1, 0.90);
    solid = 1;
    collisionGroup = CG_WALLS;
    collidesWith = CG_SOLIDPLAYER;
    Body::onCollision = [this] (Body* other) { onCollide(other); };
  }

  virtual void addActors(vector<Actor>& actors) const override
  {
    auto r = Actor { pos, MDL_SPIKES };
    r.scale = size;
    r.ratio = 0;
    actors.push_back(r);
  }

  void onCollide(Body* other)
  {
    if(auto damageable = dynamic_cast<Damageable*>(other))
      damageable->onDamage(1000);
  }
};

#include "gameplay/entity_factory.h"
static auto const reg1 = registerEntity("spikes", [] (IEntityConfig*) -> unique_ptr<Entity> { return make_unique<Spikes>(); });

