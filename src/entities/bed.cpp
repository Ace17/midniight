// Copyright (C) 2018 - Sebastien Alaiwan
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

#include "base/scene.h"
#include "base/util.h"

#include "gameplay/collision_groups.h"
#include "gameplay/entity.h"
#include "gameplay/entity_factory.h"
#include "gameplay/models.h" // MDL_BED
#include "gameplay/player.h"
#include "gameplay/sounds.h"
#include "gameplay/toggle.h"
#include "gameplay/vec.h"

namespace
{
struct ExitPoint : Entity
{
  ExitPoint()
  {
    solid = 0;
    size.width = 3;
    size.height = 1;
    collisionGroup = 0;
    collidesWith = CG_PLAYER | CG_SOLIDPLAYER;
    Body::onCollision = [this] (Body* other) { onCollide(other); };
  }

  virtual void addActors(vector<Actor>& actors) const override
  {
    auto r = Actor { pos, MDL_BED };
    r.scale = size;
    r.ratio = 0;
    r.action = 0;

    actors.push_back(r);
  }

  void onCollide(Body* other)
  {
    if(!active)
      return;

    if(auto player = dynamic_cast<Player*>(other))
    {
      const int count = player->getArtifactCount();

      if(count >= 8)
      {
        active = false;
        timer = 500;
        game->stopMusic();
        game->playSound(SND_EXPLODE);

        Body::onCollision = [this] (Body*) {};
      }
      else
      {
        game->textBox("You don't have enough artifacts");
      }
    }
  }

  virtual void tick() override
  {
    if(active)
      return;

    game->setAmbientLight(-2.0 + (timer / 500.0) * 2);

    if(decrement(timer))
    {
      game->postEvent(make_unique<FinishGameEvent>());
      active = true;
    }
  }

  bool active = true;
  int timer = 0;
};

static auto const reg1 = registerEntity("bed", [] (IEntityConfig*) -> unique_ptr<Entity> { return make_unique<ExitPoint>(); });
}

