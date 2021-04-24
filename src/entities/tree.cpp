// Display-only : tree

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
struct Tree : Entity
{
  Tree()
  {
    size = UnitSize * 4;

    collidesWith = 0;
    collisionGroup = 0;
  }

  void enter() override
  {
  }

  virtual void addActors(vector<Actor>& actors) const override
  {
    auto r = Actor { pos, MDL_TREE };
    r.scale = UnitSize * 4;
    r.ratio = (time % 30) / 30.0f;

    actors.push_back(r);
  }

  virtual void tick() override
  {
    ++time;
  }

  int time = 0;
};

struct Owl : Entity
{
  Owl()
  {
    size = UnitSize;

    collidesWith = 0;
    collisionGroup = 0;
  }

  virtual void addActors(vector<Actor>& actors) const override
  {
    auto r = Actor { pos, MDL_OWL };
    r.scale = UnitSize;
    r.ratio = 0;

    actors.push_back(r);
  }

  virtual void tick() override {}
};

auto const reg1 = registerEntity("tree", [] (IEntityConfig*) -> std::unique_ptr<Entity> { return std::make_unique<Tree>(); });
auto const reg2 = registerEntity("owl", [] (IEntityConfig*)  -> std::unique_ptr<Entity> { return std::make_unique<Owl>(); });
}

