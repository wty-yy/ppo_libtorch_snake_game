#include "env_snake.h"
#include "vec_env.h"
#include "model/mlp.h"
#include <tensorboard_logger/summary_writer.h>
#include "argparse/argparse.hpp"
#include <iostream>
#include <filesystem>
#include <string>
#include <chrono>
#include <thread>

namespace fs = std::filesystem;

int game_size = 8;
torch::Device device("cpu");

namespace fs = std::filesystem;
fs::path PATH_ROOT = tensorboard::get_root_path();
std::string PATH_CKPT = PATH_ROOT / "ckpt";
// std::string PATH_CKPT = PATH_ROOT / "best_ckpt";
int find_new_ckpt = true;
int deterministic = false;

std::string get_largest(const std::string& directory_path, bool find_folder) {
  std::string largest_folder_name, largest_folder_stem="0";

  for (const auto& entry : fs::directory_iterator(directory_path)) {
    std::string name = entry.path().filename().string();
    if (find_folder && name.find("size"+std::to_string(game_size)) == std::string::npos) continue;
    if (name > largest_folder_name)
      largest_folder_name = name;
  }

  if (!largest_folder_name.empty()) {
    return (fs::path(directory_path) / largest_folder_name).string();
  } else {
    std::cerr << "No folders found in the directory.\n";
    return "";
  }
}

int main(int argc, char* argv[]) {
  argparse::ArgumentParser parser("eval");
  parser.add_argument("--game-size").store_into(game_size).help("The size of the game scene");
  parser.add_argument("--cuda").flag().help("If triggered, use cuda, make sure device is same as model save in training");
  parser.add_argument("--path-ckpt-dir").store_into(PATH_CKPT).help("The checkpoints load path");
  parser.add_argument("--find-new-ckpt").store_into(find_new_ckpt).help("If set to 1, find newest training directory");
  parser.add_argument("--deterministic").store_into(deterministic).help("If set to 1, use deterministic policy");
  try {
    parser.parse_args(argc, argv);
  } catch(const std::exception& err) {
    std::cerr << err.what() << '\n';
    std::cerr << parser << '\n';
    exit(1);
  }
  SnakeGame game(SnakeGameOption().width(game_size).height(game_size).useRender(true));
  // while (1) {
  //   int action = rand() % 4;
  //   EnvInfo info = game.step(action);
  //   if (info.done) game.reset();
  //   sf::sleep(sf::milliseconds(10));
  // }
  auto [obs_space, action_nums] = game.get_space();
  std::string path_current_dir = find_new_ckpt ? get_largest(PATH_CKPT, true) : PATH_CKPT;
  std::cout << "current dir:" << path_current_dir << '\n';
  std::string path_model = get_largest(path_current_dir, false);
  std::cout << "current model: " << path_model << '\n';

  auto device(torch::cuda::is_available() && parser.get<bool>("cuda") ? torch::kCUDA : torch::kCPU);
  MLP model(obs_space, action_nums);
  model->to(device);
  torch::load(model, path_model);

  EnvInfo info = game.reset();
  auto start_time = std::chrono::high_resolution_clock::now();
  while (1) {
    auto obs = torch::from_blob(info.obs->data(), obs_space).to(device);
    auto [action, value] = model->forward(obs);
    int act;
    if (deterministic) {
      auto softmax_probs = torch::softmax(action, 0);
      act = torch::multinomial(softmax_probs, 1).item<int>();
    } else {
      act = action.argmax().item<int>();
    }
    info = game.step(act);
    if (info.done) {
      info = game.reset();
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(end_time-start_time).count() > 10) {
      start_time = std::chrono::high_resolution_clock::now();
      auto new_path_model = get_largest(path_current_dir, false);
      if (path_model != new_path_model) {
        torch::load(model, new_path_model);
        path_model = new_path_model;
        std::cout << "update model: " << new_path_model << '\n';
        // info = game.reset();
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return 0;
}