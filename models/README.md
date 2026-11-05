# 已训练声音模型索引

`myVoice` 智能训练成功后，模型会**自动登记**到本目录的 `index.tsv`，不需要用户手工编辑。

文件采用 UTF-8 TSV（Tab 分隔），列顺序如下：

```text
engine<TAB>name<TAB>primary_model_absolute_path<TAB>secondary_model_absolute_path
```

- **RVC**：`engine=rvc`，主权重指向 `third_party/rvc/assets/weights/<name>.pth`，辅助模型留空。
- **Seed-VC V2**：`engine=seed-vc`，主权重为 CFM 微调模型，辅助权重为 AR 基础模型（Seed-VC V2 的 `inference_v2.py` 支持分别加载这两个检查点）。
- FTXUI 中输入 `/RVC` 或 `/Seed-VC` → `/models` → `/load-model <name>`；Tab 自动提示该引擎的模型名。

该索引保存的是模型的**本地绝对路径**。移动项目目录后，旧模型条目可能失效；需要重新登记或重新训练。训练数据、模型权重和索引默认不加入 Git，以免不必要地公开私人录音或大文件。

**注意：** 注册仅确认文件存在并达到最低大小，不等于经过音质/安全性评测；仅加载你信任来源的 `.pth`，因为 PyTorch 反序列化存在执行不可信代码的风险。
