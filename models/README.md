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

## 自动试听输出

进入 `/train-wizard` 时，可选一段用于试听的声音素材。训练与模型注册成功后，程序会尝试自动生成：

```text
models/previews/<模型名>_<唯一时间戳>_preview.wav
```

Seed-VC V2 的试听还需要指定目标音色参考 WAV；没有提供试听素材时会跳过，训练与自动加载照常完成。试听转换失败也不会撤销已成功训练的模型；程序不会自动打开音频播放器，用户可用系统播放器试听。
