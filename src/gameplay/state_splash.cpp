// Copyright (C) 2018 - Sebastien Alaiwan
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

// splash menu

#include "base/scene.h"
#include "base/view.h"
#include <memory>

#include "models.h" // MDL_SPLASH
#include "sounds.h"
#include "state_machine.h"
#include "toggle.h"
#include "vec.h"

struct SplashState : Scene
{
  SplashState(View* view_) : view(view_)
  {
  }

  ////////////////////////////////////////////////////////////////
  // Scene: Game, seen by the engine

  Scene* tick(Control c) override
  {
    auto const FADE_TIME = 200;

    if(!activated)
    {
      view->playMusic(0);

      delay = FADE_TIME;

      if(c.fire || c.jump || c.dash || c.start)
      {
        view->stopMusic();
        view->playSound(SND_PAUSE);
        activated = true;
      }
    }

    view->setCameraPos(NullVector);
    view->setAmbientLight(delay / float(FADE_TIME) - 1.0);

    if(activated)
    {
      if(decrement(delay))
      {
        std::unique_ptr<Scene> deleteMeOnReturn(this);
        return createPlayingState(view);
      }
    }

    return this;
  }

  void draw() override
  {
    {
      auto splash = Actor { NullVector, MDL_SPLASH };
      splash.scale = Size2f(16, 16);
      splash.pos -= Vector2f(8, 8);
      view->sendActor(splash);
    }
  }

private:
  View* const view;
  bool activated = false;
  int delay = 0;
};

Scene* createSplashState(View* view)
{
  return new SplashState(view);
}

