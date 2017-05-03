#ifndef ONEFLOW_COPY_TASK_NODE_H_
#define ONEFLOW_COPY_TASK_NODE_H_

#include "graph/task_node.h"

namespace oneflow {

class CopyTaskNode : public TaskNode {
 public:
  OF_DISALLOW_COPY_AND_MOVE(CopyTaskNode);
  CopyTaskNode() = default;
  virtual ~CopyTaskNode() = default;

 protected:
  virtual std::shared_ptr<const Operator> ConstructOp() const = 0;

 private:
  void BuildExecAndEnrollLbn2Regsts();
  void InferShape4LbnInProducedRegsts();

};

class CopyHDTaskNode final : public CopyTaskNode {
 public:
  OF_DISALLOW_COPY_AND_MOVE(CopyHDTaskNode);
  CopyHDTaskNode() = default;
  ~CopyHDTaskNode() = default;
  
  bool IsH2D() const {
    return ((IsFwInCopy() && IsFwNode()) || (IsFwOutCopy() && IsBpNode()));
  }
  bool IsD2H() const {
    return !IsH2D();
  }

  bool IsFwInCopy() const { return is_fw_in_copy_; }
  bool IsFwOutCopy() const { return !is_fw_in_copy_; }
  void SetFwInCopy();
  void SetFwOutCopy();
  
  std::string VisualStr() const override {
    return TaskNode::VisualStr() + "CopyHD";
  }
  
 private:
  std::shared_ptr<const Operator> ConstructOp() const override;

  void InitWithFwNode(TaskNode* fw_node) override {
    TaskNode::InitWithFwNode(fw_node);
    is_fw_in_copy_ = of_dynamic_cast<CopyHDTaskNode*>(fw_node)->is_fw_in_copy_;
  }
  std::unique_ptr<TaskNode> CreateSameTypeNode() const override {
    return of_make_unique<CopyHDTaskNode> ();
  }

  bool is_fw_in_copy_;

};

class CommNetTaskNode final : public CopyTaskNode {
 public:
  OF_DISALLOW_COPY_AND_MOVE(CommNetTaskNode);
  CommNetTaskNode() = default;
  ~CommNetTaskNode() = default;

  bool IsSender() const {
    return (IsFwNode() && is_fw_sender_)
        || (IsBpNode() && !is_fw_sender_);
  }
  bool IsReceiver() const {
    return !IsSender();
  }

  void SetFwSender() {
    CHECK(IsFwNode());
    is_fw_sender_ = true;
  }
  void SetFwReceiver() {
    CHECK(IsFwNode());
    is_fw_sender_ = false;
  }
  
  std::string VisualStr() const override {
    return TaskNode::VisualStr() + "CommNet";
  }

 private:
  std::shared_ptr<const Operator> ConstructOp() const override;
  std::unique_ptr<TaskNode> CreateSameTypeNode() const override {
    return of_make_unique<CommNetTaskNode> ();
  }
  void InitWithFwNode(TaskNode* fw_node) override {
    TaskNode::InitWithFwNode(fw_node);
    is_fw_sender_ = of_dynamic_cast<CommNetTaskNode*>(fw_node)->is_fw_sender_;
  }

  bool is_fw_sender_;

};

} // namespace oneflow

#endif // ONEFLOW_COPY_TASK_NODE_H_
