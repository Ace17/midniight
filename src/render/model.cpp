// Copyright (C) 2018 - Sebastien Alaiwan
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

#include "base/error.h"
#include "base/geom.h"
#include "base/string.h"
#include "misc/file.h"
#include "misc/json.h"
#include "misc/util.h" // dirName
#include "model.h"

extern int loadTexture(String path, Rect2f rect);

static
void addTexture(Action& action, String path, Rect2f rect)
{
  action.textures.push_back(loadTexture(path, rect));
}

static
Action loadSheetAction(json::Value const& action, string sheetPath, int ROWS, int COLS)
{
  Action r;

  action["name"];

  for(auto& frame : action["frames"].elements)
  {
    auto const idx = (int)frame;

    auto const col = idx % COLS;
    auto const row = idx / COLS;
    Rect2f rect;
    rect.pos.x = col / float(COLS);
    rect.pos.y = row / float(ROWS);
    rect.size.width = 1.0 / float(COLS);
    rect.size.height = 1.0 / float(ROWS);
    addTexture(r, sheetPath, rect);
  }

  return r;
}

static
Model loadAnimatedModel(String jsonPath)
{
  auto data = File::read(jsonPath);
  Model r;
  auto obj = json::parse(data.c_str(), data.size());
  auto dir = dirName(jsonPath);

  auto type = string(obj["type"]);
  auto const cols = (int)obj["cols"];
  auto const rows = (int)obj["rows"];

  if(type == "sheet")
  {
    std::string sheet = setExtension(jsonPath, "png");

    for(auto& action : obj["actions"].elements)
      r.actions.push_back(loadSheetAction(action, sheet, rows, cols));
  }
  else if(type == "tiled")
  {
    auto sheet = string(obj["sheet"]);

    for(int row = 0; row < rows; ++row)
    {
      for(int col = 0; col < rows; ++col)
      {
        Action action;
        Rect2f rect;
        rect.pos.x = col / float(cols);
        rect.pos.y = row / float(rows);
        rect.size.width = 1.0 / float(cols);
        rect.size.height = 1.0 / float(rows);

        addTexture(action, (string(dir.data, dir.len) + "/" + sheet), rect);
        r.actions.push_back(action);
      }
    }
  }
  else
    throw Error("Unknown model type: '" + type + "'");

  return r;
}

static
Model loadTiledModel(String path, int count, int COLS, int ROWS)
{
  auto m = Model();

  for(int i = 0; i < count; ++i)
  {
    auto col = i % COLS;
    auto row = i / COLS;

    auto const width = 1.0 / float(COLS);
    auto const height = 1.0 / float(ROWS);

    Action action;
    addTexture(action, path, Rect2f(col * width, row * height, width, height));
    m.actions.push_back(action);
  }

  return m;
}

Model loadModel(String path)
{
  try
  {
    if(endsWith(path, ".model"))
    {
      if(!File::exists(path))
      {
        printf("[display] model '%.*s' doesn't exist, fallback on default model\n", path.len, path.data);
        path = "res/sprites/rect.model";
      }

      return loadAnimatedModel(path);
    }
    else if(endsWith(path, ".tiles"))
    {
      auto pngPath = setExtension(path, "png");

      if(!File::exists(pngPath))
      {
        printf("[display] tileset '%s' was not found, fallback on default tileset\n", pngPath.c_str());
        pngPath = "res/tiles/default.png";
      }

      return loadTiledModel(pngPath, 64, 8, 8);
    }
    else
    {
      throw Error("unknown format for '" + string(path.data, path.len) + "'");
    }
  }
  catch(const Error& e)
  {
    throw Error("When loading '" + string(path.data, path.len) + "': " + string(e.message().data, e.message().len));
  }
}

