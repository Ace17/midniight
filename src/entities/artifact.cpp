// Collectible artifact entity

#include <cmath> // sin

#include "base/scene.h"
#include "base/util.h"

#include "gameplay/collision_groups.h"
#include "gameplay/entity.h"
#include "gameplay/entity_factory.h"
#include "gameplay/models.h"
#include "gameplay/player.h"
#include "gameplay/sounds.h"

namespace
{
struct Artifact : Entity
{
  Artifact()
  {
    size = UnitSize;
    Body::onCollision = [this] (Body* other) { onCollide(other); };

    collidesWith = CG_SOLIDPLAYER;
    collisionGroup = CG_BONUS;
  }

  void enter() override
  {
    auto var = game->getVariable(id);

    // already picked up?
    if(var->get())
      dead = true;
  }

  virtual void addActors(vector<Actor>& actors) const override
  {
    auto r = Actor { pos, MDL_ARTIFACT };
    r.scale = UnitSize;
    r.effect = Effect::Blinking;
    r.ratio = (time % 20) / 20.0f;

    actors.push_back(r);
  }

  virtual void tick() override
  {
    ++time;
  }

  void onCollide(Body* other)
  {
    if(dead)
      return;

    if(auto player = dynamic_cast<Player*>(other))
    {
      player->addArtifact();
      game->playSound(SND_ARTIFACT);

      dead = true;

      auto var = game->getVariable(id);
      var->set(1);
    }
  }

  int time = 0;
};

std::unique_ptr<Entity> makeArtifact(IEntityConfig* cfg)
{
  (void)cfg;
  return make_unique<Artifact>();
}

auto const reg1 = registerEntity("artifact", [] (IEntityConfig* cfg) { return makeArtifact(cfg); });
}

