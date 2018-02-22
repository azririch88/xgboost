/*!
* Copyright 2017 by Contributors
* \file xgbfi.cc
* \brief xgb feature interactions (xgbfi)
* \author Mathias M�ller (Far0n)
*/
#include "xgbfi.h"
#include <xgboost/logging.h>
#include <iostream>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <memory>
#include <map>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <set>
// #include <omp.h>
// #include <regex>

namespace xgbfi {
class XgbTreeNode;
class XgbTree;
class XgbModel;
class FeatureInteraction;

typedef std::vector<std::string> XgbModelDump;
typedef std::shared_ptr<XgbTree> XgbTreePtr;
typedef std::map<int, XgbTreeNode> XgbNodeList;
typedef std::vector<XgbNodeList> XgbNodeLists;
typedef std::map<std::string, FeatureInteraction> FeatureInteractions;
typedef std::vector<XgbTreeNode> InteractionPath;
typedef std::unordered_set<std::size_t> PathMemo;

/*!
* \brief xgbfi tree node type
*/
enum XgbNodeType { None, BinaryFeature, NumericFeature, Leaf };

/*!
* \brief xgbfi xgb-model parser
*/
class XgbModelParser {
 public:
  static XgbModel GetXgbModelFromDump(const XgbModelDump& dump, int max_trees = -1,
  int nthread = 1);

  static XgbTreeNode ParseXgbTreeNode(std::string* line);

 private:
  static void ConstructXgbTree(XgbTreePtr tree, const XgbNodeList &nodes);
  // static const std::regex node_regex_;
  // static const std::regex leaf_regex_;
};

/*!
* \brief xgbfi xgb-model tree node
*/
class XgbTreeNode {
 public:
  int number;
  std::string feature;
  double gain;
  double cover;
  int left_child;
  int right_child;
  double split_value;
  double leaf_value;
  bool is_leaf;

  XgbTreeNode() : number(0), feature(""), gain(0), cover(0),
    left_child(-1), right_child(-1), split_value(0), leaf_value(0), is_leaf(false) { }
};


/*!
* \brief xgbfi xgb-model tree
*/
class XgbTree {
 public:
  int number;
  XgbTreeNode root;
  XgbTreePtr left;
  XgbTreePtr right;

  explicit XgbTree(XgbTreeNode root, int number = 0)
    : number(number), root(root) { }
};

/*!
* \brief xgbfi feature interaction
*/
class FeatureInteraction {
 public:
  std::string name;
  int depth;
  double gain;
  double cover;
  double fscore;
  double w_fscore;
  double avg_w_fscore;
  double avg_gain;
  double expected_gain;

  FeatureInteraction() { }
  FeatureInteraction(const InteractionPath&  interaction,
    double gain, double cover, double path_proba, double depth, double fScore = 1);

  operator std::string() const {
    std::ostringstream oos;
    oos << name << ',' << depth << ',' << gain << ',' << fscore << ','
      << w_fscore << ',' << avg_w_fscore << ',' << avg_gain << ',' << expected_gain;
    return oos.str();
  }
  
  static std::size_t InteractionPathToHash(const InteractionPath& interaction_path);

  static std::string InteractionPathToStr(const InteractionPath& interaction_path,
    const bool encode_path = false, const bool sort_by_feature = true);

  static void Merge(FeatureInteractions* lfis, const FeatureInteractions& rfis);
};

/*!
* \brief xgbfi xgb-model
*/
class XgbModel {
 public:
  std::vector<XgbTreePtr> trees;
  int ntrees;

 private:
  int max_interaction_depth_;
  int max_tree_depth_;
  int max_deepening_;

  void CollectFeatureInteractions(XgbTreePtr tree, InteractionPath* cfi,
    double current_gain, double current_cover, double path_proba, int depth, int deepening,
    FeatureInteractions* tfis, PathMemo* memo);

 public:
  FeatureInteractions GetFeatureInteractions(int max_interaction_depth = -1,
    int max_tree_depth = -1,
    int max_deepening = -1,
    int nthread = 1);

  explicit XgbModel(int ntrees) : ntrees(ntrees) {
    trees.resize(ntrees);
  }
  XgbModel() : ntrees(0) { }
};

/*!
* \brief xgbfi string utilities
*/
class StringUtils {
 public:
  // static inline std::vector<std::string> split(const std::string& s, const std::string& regex) {
  //   std::regex re(regex);
  //   std::sregex_token_iterator itr {s.begin(), s.end(), re, -1 };
  //   return { itr, {} };
  // }

  static inline std::vector<std::string> split(const char* str, const char delim) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string tok;
    while (std::getline(iss, tok, delim)) {
      tokens.push_back(tok);
    }
    return tokens;
  }

  template <typename T>
  std::string join(const T& vec, const std::string& delim) {
    std::ostringstream s;
    for (const auto& i : vec) {
      if (&i != &vec[0]) {
        s << delim;
      }
      s << i;
    }
    return s.str();
  }

  static inline void ltrim(std::string* s) {
    s->erase(s->begin(), std::find_if(s->begin(), s->end(), [](int ch) {
      return !std::isspace(ch);
    }));
  }

  static inline void rtrim(std::string* s) {
    s->erase(std::find_if(s->rbegin(), s->rend(), [](int ch) {
      return !std::isspace(ch);
    }).base(), s->end());
  }

  static inline void trim(std::string* s) {
    ltrim(s);
    rtrim(s);
  }
};

/*
const std::regex XgbModelParser::node_regex_(
  R"((\d+):\[([^<]*)<?(.+)?\]\s+yes=(\d+),no=(\d+).*?,gain=(.*),cover=(.*))",
  std::regex::optimize);
const std::regex XgbModelParser::leaf_regex_(
  R"((\d+):leaf=(.*),cover=(.*))",
  std::regex::optimize);
*/

/*
XgbModelDump XgbModelParser::ReadModelDump(const std::string& file_path, int max_trees) {
  XgbModelDump dump;
  if (max_trees == 0) return dump;

  std::ifstream ifs(file_path);
  if (!ifs) {
    std::cerr << "could not read file " << file_path << std::endl;
    return dump;
  }
  std::string line;
  std::ostringstream str_buffer;
  while (std::getline(ifs, line)) {
    if (line.find_first_of("booster") == 0) {
      if (str_buffer.tellp() > 0) {
        if (--max_trees == 0) break;
        dump.push_back(str_buffer.str());
        // str_buffer = std::ostringstream();
        str_buffer.clear();
        str_buffer.str({});
      }
      continue;
    }
    str_buffer << line << std::endl;
  }
  dump.push_back(str_buffer.str());
  return dump;
}
*/

/*
XgbModel XgbModelParser::GetXgbModelFromDump(const std::string& file_path, int max_trees) {
  return XgbModelParser::GetXgbModelFromDump(XgbModelParser::ReadModelDump(file_path,
                                                                            max_trees),
                                                                            max_trees);
}
*/

XgbModel XgbModelParser::GetXgbModelFromDump(const XgbModelDump& dump, int max_trees,
  int nthread) {
  std::cout << "COOL1" << std::endl;
  const int nthreadmax = std::max(omp_get_num_procs() / 2 - 1, 1);
  //  const int nthreadmax = omp_get_max_threads();
  if (nthread <= 0) nthread=nthreadmax;


  std::cout << "COOL2" << std::endl;
  int n_backup = omp_get_num_threads();
  std::cout << "COOL3" << std::endl;
  int ntrees = static_cast<int>(dump.size());
  std::cout << "COOL4: ntrees " << ntrees << std::endl;
  if ((max_trees < ntrees) && (max_trees >= 0)) ntrees = max_trees;

  std::cout << "COOL5" << std::endl;
  XgbModel xgb_model(ntrees);
  std::cout << "COOL6" << std::endl;
  XgbNodeLists xgb_node_lists = {};

  for (int i = 0; i < ntrees; ++i) {
    std::cout << "COOL7 " << i << std::endl;
    xgb_node_lists.push_back(XgbNodeList{});
  }

  std::cout << "COOL8" << std::endl;
  omp_set_num_threads(nthread);
//#pragma omp parallel for schedule(auto) num_threads(nthread)
  for (int i = 0; i < ntrees; ++i) {
    std::cout << "COOL9 " << i << std::endl;
    std::istringstream iss(dump[i]);
    std::string line;
    auto nodes = &xgb_node_lists[i];
    while (std::getline(iss, line)) {
      auto&& xgb_tree_node = ParseXgbTreeNode(&line);
      (*nodes)[xgb_tree_node.number] = std::move(xgb_tree_node);
    }
  }

//#pragma omp parallel for schedule(auto) num_threads(nthread)
  for (int i = 0; i < ntrees; ++i) {
    std::cout << "COOL10a " << i << std::endl;
    auto&& tree = std::make_shared<XgbTree>(XgbTree(xgb_node_lists[i][0], i));
    std::cout << "COOL10b " << i << std::endl;
    ConstructXgbTree(tree, xgb_node_lists[i]);
    std::cout << "COOL10c " << i << std::endl;
    xgb_model.trees[i] = std::move(tree);
  }

  std::cout << "COOL11" << std::endl;
  omp_set_num_threads(n_backup);
  return xgb_model;
}


void XgbModelParser::ConstructXgbTree(XgbTreePtr tree, const XgbNodeList& nodes) {
  if (tree->root.left_child != -1) {
    std::cout << "HELLO1a " << tree->root.left_child << std::endl;
    tree->left = std::make_shared<XgbTree>(XgbTree(nodes.at(tree->root.left_child)));
    std::cout << "HELLO1b " << tree->root.left_child << std::endl;
    ConstructXgbTree(tree->left, nodes);
    std::cout << "HELLO1c " << tree->root.left_child << std::endl;
  }
  if (tree->root.right_child != -1) {
    std::cout << "HELLO2a " << tree->root.left_child << std::endl;
    tree->right = std::make_shared<XgbTree>(XgbTree(nodes.at(tree->root.right_child)));
    std::cout << "HELLO2b " << tree->root.left_child << std::endl;
    ConstructXgbTree(tree->right, nodes);
    std::cout << "HELLO3b " << tree->root.left_child << std::endl;
  }
}


XgbTreeNode XgbModelParser::ParseXgbTreeNode(std::string* line) {
  StringUtils::trim(line);

  XgbTreeNode xgb_tree_node;
  XgbNodeType node_type = XgbNodeType::None;

  auto ix = line->find_first_of(':');
  if (ix == std::string::npos || ix == line->length() - 1) {
    return xgb_tree_node;
  }

  if ((*line)[ix + 1] != '[') {
    node_type = XgbNodeType::Leaf;
  } else if (line->find_first_of('<') != std::string::npos) {
    node_type = XgbNodeType::NumericFeature;
  } else {
    node_type = XgbNodeType::BinaryFeature;
  }
  xgb_tree_node.number = std::stol(line->substr(0, ix), nullptr, 10);

  if (node_type == XgbNodeType::Leaf) {
    xgb_tree_node.is_leaf = true;

    auto lx = line->find_first_of('=') + 1;
    auto rx = line->find_first_of(",", lx);
    xgb_tree_node.leaf_value = std::stod(line->substr(lx, rx - lx), nullptr);

    lx = line->find_first_of('=', rx) + 1;
    rx = line->size();
    xgb_tree_node.cover = std::stod(line->substr(lx, rx - lx), nullptr);

    return xgb_tree_node;
  }

  xgb_tree_node.is_leaf = false;
  auto lx = line->find_first_of('[') + 1;
  auto rx = line->find_first_of("<]", lx);
  xgb_tree_node.feature = line->substr(lx, rx - lx);

  if (node_type == XgbNodeType::BinaryFeature) {
    xgb_tree_node.split_value = 1.0;
  } else {
    lx = rx + 1;
    rx = line->find_first_of(']', lx);
    xgb_tree_node.split_value = std::stod(line->substr(lx, rx - lx), nullptr);
  }
  lx = line->find_first_of('=') + 1;
  rx = line->find_first_of(",", lx);
  xgb_tree_node.left_child = std::stol(line->substr(lx, rx - lx), nullptr, 10);

  lx = line->find_first_of('=', rx) + 1;
  rx = line->find_first_of(",", lx);
  xgb_tree_node.right_child = std::stol(line->substr(lx, rx - lx), nullptr, 10);

  lx = line->find_first_of('=', line->find("gain", rx)) + 1;
  rx = line->find_first_of(",", lx);
  xgb_tree_node.gain = std::stod(line->substr(lx, rx - lx), nullptr);

  lx = line->find_first_of('=', rx) + 1;
  rx = line->size();
  xgb_tree_node.cover = std::stod(line->substr(lx, rx - lx), nullptr);

  return xgb_tree_node;
}

std::size_t FeatureInteraction::InteractionPathToHash(
  const InteractionPath& interaction_path) {
  std::size_t hash = interaction_path.size();
  for (auto& x : interaction_path) {
    hash ^= x.number + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  }
  return hash;
}

std::string FeatureInteraction::InteractionPathToStr(
  const InteractionPath& interaction_path,
  const bool encode_path, const bool sort_by_feature) {
  auto vec = std::vector<XgbTreeNode>{ interaction_path };
  if (sort_by_feature && !encode_path) {
    std::sort(vec.begin(), vec.end(), [](const XgbTreeNode &a, const XgbTreeNode &b) {
      return a.feature < b.feature;
    });
  }
  std::string path_str;
  std::string delim = encode_path ? "-" : "|";
  for (const auto& x : vec) {
    if (&x != &vec[0]) {
      path_str += delim;
    }
    path_str += (encode_path ? std::to_string(x.number) : x.feature);
  }
  return path_str;
}


void FeatureInteraction::Merge(FeatureInteractions* lfis, const FeatureInteractions& rfis ) {
  for (auto& rfi_kv : rfis) {
    if (lfis->find(rfi_kv.first) == lfis->end()) {
      (*lfis)[rfi_kv.first] = rfi_kv.second;
    } else {
      auto lfi = &(*lfis)[rfi_kv.first];
      auto rfi = &rfi_kv.second;
      lfi->gain += rfi->gain;
      lfi->cover += rfi->cover;
      lfi->fscore += rfi->fscore;
      lfi->w_fscore += rfi->w_fscore;
      lfi->avg_w_fscore = lfi->w_fscore / lfi->fscore;
      lfi->avg_gain = lfi->gain / lfi->fscore;
      lfi->expected_gain += rfi->expected_gain;
    }
  }
}


FeatureInteraction::FeatureInteraction(const InteractionPath& interaction_path,
  double gain, double cover, double path_proba, double depth, double fScore) {
  this->name = InteractionPathToStr(interaction_path);
  this->depth = static_cast<int>(interaction_path.size()) - 1;
  this->gain = gain;
  this->cover = cover;
  this->fscore = fScore;
  this->w_fscore = path_proba;
  this->avg_w_fscore = this->w_fscore / this->fscore;
  this->avg_gain = this->gain / this->fscore;
  this->expected_gain = this->gain * path_proba;
}


FeatureInteractions XgbModel::GetFeatureInteractions(int max_interaction_depth,
                                                     int max_tree_depth,
                                                     int max_deepening,
                                                     int nthread) {
  const int nthreadmax = std::max(omp_get_num_procs() / 2 - 1, 1);
  //  const int nthreadmax = omp_get_max_threads();
  std::cout << "RUN1" << std::endl;
  if (nthread <= 0) nthread=nthreadmax;

  std::cout << "RUN2" << std::endl;
  int n_backup = omp_get_num_threads();
  std::cout << "RUN3" << std::endl;
  max_interaction_depth_ = max_interaction_depth;
  std::cout << "RUN4" << std::endl;
  max_tree_depth_ = max_tree_depth;
  std::cout << "RUN5" << std::endl;
  max_deepening_ = max_deepening;

  std::cout << "RUN6" << std::endl;
  std::vector<FeatureInteractions> trees_feature_interactions(ntrees);

  //LOG(CONSOLE) << "Start get feature interactions from model parallel";
  std::cout << "RUN7" << std::endl;
  omp_set_num_threads(nthread);
#pragma omp parallel for schedule(auto) num_threads(nthread)
  for (int i = 0; i < ntrees; ++i) {
  std::cout << "RUN8 " << i << std::endl;
    FeatureInteractions tfis{};
    InteractionPath cfi{};
    PathMemo memo{};
    CollectFeatureInteractions(trees[i], &cfi, 0, 0, 1, 0, 0, &tfis, &memo);
    trees_feature_interactions[i] = tfis;
  }
  //LOG(CONSOLE) << "End get feature interactions from model parallel";

  std::cout << "RUN9" << std::endl;
  FeatureInteractions fis;

/*
#if(_OPENMP >= 201307) //OPENMP 4.0+
#pragma omp declare reduction \
(merge:FeatureInteractions:FeatureInteraction::Merge(&omp_out,omp_in)) \
initializer(omp_priv={})
#pragma omp parallel for reduction(merge:fis)
#endif //OPENMP 4.0+
*/  
  std::cout << "RUN10" << std::endl;
  //LOG(CONSOLE) << "Start get feature interactions from model merge";
  for (int i = 0; i < ntrees; ++i) {
    FeatureInteraction::Merge(&fis, trees_feature_interactions[i]);
  }
  std::cout << "RUN11" << std::endl;
  //LOG(CONSOLE) << "End get feature interactions from model merge";

  omp_set_num_threads(n_backup);
  std::cout << "RUN12" << std::endl;
  return fis;
}


void XgbModel::CollectFeatureInteractions(XgbTreePtr tree, InteractionPath* cfi,
  double current_gain, double current_cover, double path_proba, int depth, int deepening,
  FeatureInteractions* tfis, PathMemo* memo) {
  if (tree->root.is_leaf || depth == max_tree_depth_) {
    return;
  }
  cfi->push_back(tree->root);
  current_gain += tree->root.gain;
  current_cover += tree->root.cover;

  auto ppl = path_proba * (tree->left->root.cover / tree->root.cover);
  auto ppr = path_proba * (tree->right->root.cover / tree->root.cover);

  FeatureInteraction fi(*cfi, current_gain, current_cover, path_proba, depth, 1);

  if ((depth < max_deepening_) || (max_deepening_ < 0)) {
    InteractionPath ipl{};
    InteractionPath ipr{};

    CollectFeatureInteractions(tree->left, &ipl, 0, 0, ppl, depth + 1, deepening + 1, tfis, memo);
    CollectFeatureInteractions(tree->right, &ipr, 0, 0, ppr, depth + 1, deepening + 1, tfis, memo);
  }

  auto path = FeatureInteraction::InteractionPathToHash(*cfi);

  if (tfis->find(fi.name) == tfis->end()) {
    (*tfis)[fi.name] = fi;
    memo->insert(path);
  } else {
    if (memo->count(path)) {
      return;
    }
    memo->insert(path);
    auto tfi = &(*tfis)[fi.name];
    tfi->gain += current_gain;
    tfi->cover += current_cover;
    tfi->fscore += 1;
    tfi->w_fscore += path_proba;
    tfi->avg_w_fscore = tfi->w_fscore / tfi->fscore;
    tfi->avg_gain = tfi->gain / tfi->fscore;
    tfi->expected_gain += current_gain * path_proba;
  }

  if (static_cast<int>(cfi->size()) - 1 == max_interaction_depth_)
    return;

  InteractionPath ipl{ *cfi };
  InteractionPath ipr{ *cfi };

  CollectFeatureInteractions(tree->left, &ipl, current_gain, current_cover,
                             ppl, depth + 1, deepening, tfis, memo);
  CollectFeatureInteractions(tree->right, &ipr, current_gain, current_cover,
                             ppr, depth + 1, deepening, tfis, memo);
}


std::vector<std::string> GetFeatureInteractions(const xgboost::Learner& learner,
  int max_fi_depth, int max_tree_depth, int max_deepening, int ntrees, const char* fmap,
  int nthread) {

  std::cout << "STRING1" << std::endl;
  const int nthreadmax = std::max(omp_get_num_procs() / 2 - 1, 1);
  std::cout << "STRING2" << std::endl;
  //  const int nthreadmax = omp_get_max_threads();
  if (nthread <= 0) nthread=nthreadmax;
  std::cout << "STRING3" << std::endl;
  int n_backup = omp_get_max_threads();
  std::cout << "STRING4" << std::endl;
  omp_set_num_threads(nthread);
  std::cout << "STRING5" << std::endl;

  std::vector<std::string> feature_interactions;
  std::cout << "STRING6" << std::endl;
  xgboost::FeatureMap feature_map;
  std::cout << "STRING7" << std::endl;
  if (strchr(fmap, '|') != NULL) {
    int fnum = 0;
    const char* ftype = "q";
    for (auto feat : StringUtils::split(fmap, '|')) {
      std::cout << "STRING7 " << fnum << std::endl;
      feature_map.PushBack(fnum++, feat.c_str(), ftype);
    }
  } else if (*fmap != '\0') {
    try {
      std::cout << "STRING8" << std::endl;
      std::unique_ptr<dmlc::Stream> fs(dmlc::Stream::Create(fmap, "r"));
      dmlc::istream is(fs.get());
      feature_map.LoadText(is);
    }
    catch (...) {
      LOG(CONSOLE) << "Warning: unable to read feature map: \"" << fmap << "\", "
        "feature names wont be mapped";
    }
  }
  std::cout << "STRING9" << std::endl;
  //LOG(CONSOLE) << "Start dump model";
  auto dump = learner.DumpModel(feature_map, true, "text");
  std::cout << "STRING10" << std::endl;
  //LOG(CONSOLE) << "End dump model";
  std::cout << "STRING11" << std::endl;
  if (dump.size() == 0) {
    return feature_interactions;
  }
  std::cout << "STRING12" << std::endl;
  if (dump[0].find_first_of("bias") == 0) {
    return feature_interactions;
  }
  std::cout << "STRING13: " << ntrees << nthread << std::endl;
  //LOG(CONSOLE) << "Start model from dump";
  auto model = xgbfi::XgbModelParser::GetXgbModelFromDump(dump, ntrees, nthread);
  //LOG(CONSOLE) << "End model from dump";
  //LOG(CONSOLE) << "Start get feature interactions from model";
  std::cout << "STRING14" << std::endl;
  auto fi = model.GetFeatureInteractions(max_fi_depth,
                                         max_tree_depth,
                                         max_deepening,
                                         nthread);
  std::cout << "STRING15" << std::endl;
  //LOG(CONSOLE) << "End get feature interactions from model";
  for (auto kv : fi) {
    feature_interactions.push_back(static_cast<std::string>(kv.second));
  }

  std::cout << "STRING16" << std::endl;
  // restore omp state
  omp_set_num_threads(n_backup);
  std::cout << "STRING17" << std::endl;

  return feature_interactions;
}
}  // namespace xgbfi
