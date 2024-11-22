#include "torch/torch.h"
#include <iostream>
#include <memory>
#include <vector>

const int game_size = 8;
using namespace torch;

class CNNImpl : public nn::Module {
 public:
  CNNImpl(std::vector<int> obs_shape, int action_nums) : 
  policy_output(nn::LinearOptions(256, action_nums).bias(false)),
  value_output(nn::LinearOptions(256, 1).bias(false)),
  obs_shape(obs_shape) {
    feature_cnn->push_back(nn::Conv2d(obs_shape[0], 32, 4));
    feature_cnn->push_back(nn::ReLU());
    feature_cnn->push_back(nn::Conv2d(32, 64, 4));
    feature_cnn->push_back(nn::ReLU());
    register_module("feature_cnn", feature_cnn);
    register_module("policy_output", policy_output);
    register_module("value_output", value_output);
  }

  std::pair<Tensor, Tensor> forward(Tensor x) {
    x = x.view({-1, obs_shape[0], obs_shape[1], obs_shape[2]});
    Tensor z = feature_cnn->forward(x).view({-1, 256});
    Tensor logist = policy_output->forward(z);
    Tensor value = value_output->forward(z);
    return {logist, value};
  }

  std::tuple<Tensor, Tensor, Tensor, Tensor> get_action_and_value(Tensor x, Tensor action={}) {
    auto [logits, value] = forward(x);
    auto probs = softmax(logits, -1);
    if (action.numel() == 0) {
      action = multinomial(probs, 1).squeeze(1);
    }
    auto logprob = log(probs.gather(-1, action.view({-1, 1}))).squeeze(1);
    auto entropy = -(probs * log(probs + 1e-18)).sum(-1);
    return std::make_tuple(action, logprob, entropy, value);
  }

  Tensor get_value(Tensor x) {
    Tensor z = feature_cnn->forward(x).view({-1, 256});
    Tensor value = value_output->forward(z);
    return value;
  }
    
  private:
    nn::Sequential feature_cnn;
    nn::Linear policy_output;
    nn::Linear value_output;
    std::vector<int> obs_shape;
};

TORCH_MODULE(CNN);

int main() {
  torch::manual_seed(42);
  auto device = torch::cuda::is_available() ? torch::kCUDA : torch::kCPU;
  std::cout << "device: " << device << '\n';
  auto x = torch::rand({game_size, game_size}).to(device);
  std::vector<int> obs_shape({1, 8, 8});
  CNN model(obs_shape, 4);
  model->to(device);
  auto [logits, value] = model->forward(x);
  std::cout << logits << '\n' << value << '\n';
  std::cout << x.device() << '\n';
  return 0;
}