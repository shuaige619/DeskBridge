/*
 * DeskBridge -- personal keyboard shortcut preferences
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/ShortcutRules.h"

#include "common/Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <cmath>
#include <algorithm>
#include <limits>

namespace deskflow::shortcuts {
namespace {

constexpr auto kModifiers = KeyModifierControl | KeyModifierShift | KeyModifierAlt | KeyModifierSuper;
constexpr auto kLocks = KeyModifierCapsLock | KeyModifierNumLock | KeyModifierScrollLock;

void setError(QString *error, const QString &message)
{
  if (error)
    *error = message;
}

bool validKey(KeyID key)
{
  if ((key >= 0x20 && key <= 0x7e) || (key >= kKeyF1 && key <= kKeyF24))
    return true;
  switch (key) {
  case kKeyBackSpace:
  case kKeyTab:
  case kKeyReturn:
  case kKeyEscape:
  case kKeyDelete:
  case kKeyHome:
  case kKeyLeft:
  case kKeyUp:
  case kKeyRight:
  case kKeyDown:
  case kKeyPageUp:
  case kKeyPageDown:
  case kKeyEnd:
  case kKeyInsert:
    return true;
  default:
    return false;
  }
}

QString appListError(const QStringList &apps)
{
  QSet<QString> seen;
  for (const auto &app : apps) {
    if (app.isEmpty() || app != app.trimmed() || app.contains(QChar::Null))
      return QStringLiteral("应用标识不能为空或包含首尾空格。");
    if (seen.contains(app))
      return QStringLiteral("应用列表包含重复项：%1").arg(app);
    seen.insert(app);
  }
  return {};
}

QSet<QString> effectiveApps(const Rule &rule, const QStringList &global)
{
  QSet<QString> result;
  for (const auto &app : rule.apps) {
    if (global.contains(app))
      result.insert(app);
  }
  return result;
}

QString conflictError(const Configuration &config)
{
  for (qsizetype i = 0; i < config.rules.size(); ++i) {
    const auto &a = config.rules[i];
    if (!a.enabled)
      continue;
    const auto apps = effectiveApps(a, config.apps);
    for (qsizetype j = i + 1; j < config.rules.size(); ++j) {
      const auto &b = config.rules[j];
      if (b.enabled && normalizedKey(a.input.key) == normalizedKey(b.input.key) &&
          a.input.modifiers == b.input.modifiers && apps.intersects(effectiveApps(b, config.apps)))
        return QStringLiteral("规则“%1”与“%2”的来源快捷键和生效应用重叠。").arg(a.name, b.name);
    }
  }
  return {};
}

bool exactFields(const QJsonObject &object, const QStringList &keys)
{
  if (object.size() != keys.size())
    return false;
  for (const auto &key : keys) {
    if (!object.contains(key))
      return false;
  }
  return true;
}

std::optional<QStringList> stringList(const QJsonValue &value)
{
  if (!value.isArray())
    return std::nullopt;
  QStringList result;
  for (const auto &item : value.toArray()) {
    if (!item.isString())
      return std::nullopt;
    result.push_back(item.toString());
  }
  return result;
}

std::optional<uint32_t> integer(const QJsonValue &value)
{
  if (!value.isDouble())
    return std::nullopt;
  const double number = value.toDouble();
  if (!std::isfinite(number) || number < 0 || number > std::numeric_limits<uint32_t>::max() ||
      number != std::floor(number))
    return std::nullopt;
  return static_cast<uint32_t>(number);
}

std::optional<Chord> readChord(const QJsonValue &value)
{
  if (!value.isObject())
    return std::nullopt;
  const auto object = value.toObject();
  if (!exactFields(object, {QStringLiteral("key"), QStringLiteral("modifiers")}))
    return std::nullopt;
  const auto key = integer(object.value(QStringLiteral("key")));
  const auto mask = integer(object.value(QStringLiteral("modifiers")));
  if (!key || !mask)
    return std::nullopt;
  return Chord{*key, *mask};
}

QJsonObject writeChord(const Chord &chord)
{
  return {{QStringLiteral("key"), static_cast<qint64>(chord.key)},
          {QStringLiteral("modifiers"), static_cast<qint64>(chord.modifiers)}};
}

Configuration closedConfiguration()
{
  Configuration config;
  config.enabled = false;
  return config;
}

} // namespace

KeyID normalizedKey(KeyID key)
{
  return key >= 'A' && key <= 'Z' ? key + ('a' - 'A') : key;
}

bool isReservedInput(const Chord &chord)
{
  return chord.key == ' ' && (chord.modifiers & ~kLocks) == KeyModifierControl;
}

QList<Rule> defaultRules(const QStringList &apps)
{
  return {
      {QStringLiteral("copy"), QStringLiteral("复制"), {'c', KeyModifierControl}, {'c', KeyModifierSuper}, true, apps},
      {QStringLiteral("paste"), QStringLiteral("粘贴"), {'v', KeyModifierControl}, {'v', KeyModifierSuper}, true, apps}
  };
}

Configuration defaults()
{
  Configuration config;
#ifdef Q_OS_MACOS
  config.apps = Settings::defaultValue(Settings::Client::ClipboardShortcutApps).toStringList();
#endif
  config.rules = defaultRules(config.apps);
  return config;
}

DeviceOS localOS()
{
#if defined(Q_OS_MACOS)
  return DeviceOS::MacOS;
#elif defined(Q_OS_WIN)
  return DeviceOS::Windows;
#elif defined(Q_OS_LINUX)
  return DeviceOS::Linux;
#else
  return DeviceOS::Unknown;
#endif
}

QString deviceOSName(DeviceOS os)
{
  switch (os) {
  case DeviceOS::MacOS: return QStringLiteral("macos");
  case DeviceOS::Windows: return QStringLiteral("windows");
  case DeviceOS::Linux: return QStringLiteral("linux");
  default: return QStringLiteral("unknown");
  }
}

QString habitName(Habit habit)
{
  switch (habit) {
  case Habit::Automatic: return QStringLiteral("automatic");
  case Habit::Mac: return QStringLiteral("mac");
  case Habit::PC: return QStringLiteral("pc");
  default: return QStringLiteral("custom");
  }
}

DeviceOS effectiveSourceOS(const Configuration &config, DeviceOS detected)
{
  const auto valid = [](DeviceOS os) { return os >= DeviceOS::Unknown && os <= DeviceOS::Linux; };
  if (valid(detected) && detected != DeviceOS::Unknown)
    return detected;
  return valid(config.sourceOS) ? config.sourceOS : DeviceOS::Unknown;
}

std::optional<ModifierMapping> shortcutModifierMapping(const Configuration &config, DeviceOS detected, DeviceOS target)
{
  if (target != DeviceOS::MacOS && target != DeviceOS::Windows && target != DeviceOS::Linux)
    return std::nullopt;
  bool macHabit = false;
  switch (config.habit) {
  case Habit::Custom:
    return std::nullopt;
  case Habit::Mac:
    macHabit = true;
    break;
  case Habit::PC:
    break;
  case Habit::Automatic: {
    const auto source = effectiveSourceOS(config, detected);
    if (source == DeviceOS::Unknown)
      return std::nullopt;
    macHabit = source == DeviceOS::MacOS;
    break;
  }
  default:
    return std::nullopt;
  }
  const auto source = macHabit ? KeyModifierSuper : KeyModifierControl;
  const auto destination = target == DeviceOS::MacOS ? KeyModifierSuper : KeyModifierControl;
  if (source == destination)
    return std::nullopt;
  return ModifierMapping{source, destination};
}

Configuration effectiveConfiguration(const Configuration &config, DeviceOS detected, DeviceOS target)
{
  if (config.habit == Habit::Custom)
    return config;
  auto effective = config;
  const auto mapping = shortcutModifierMapping(config, detected, target);
  for (auto it = effective.rules.begin(); it != effective.rules.end();) {
    if (it->id != QStringLiteral("copy") && it->id != QStringLiteral("paste")) {
      ++it;
      continue;
    }
    // Same-habit pairs should retain the target's native behavior. Unknown
    // peers must not accidentally inherit a previously connected source.
    if (!mapping) {
      it = effective.rules.erase(it);
      continue;
    }
    const KeyID key = it->id == QStringLiteral("copy") ? 'c' : 'v';
    it->input = {key, mapping->source};
    it->output = {key, mapping->target};
    ++it;
  }
  return effective;
}

void setHabit(Configuration &config, Habit habit)
{
  config.habit = habit;
  for (const auto &rule : defaultRules(config.apps)) {
    if (std::none_of(config.rules.cbegin(), config.rules.cend(), [&rule](const auto &item) { return item.id == rule.id; }))
      config.rules.push_back(rule);
  }
}

QString habitDescription(const Configuration &config, DeviceOS detected, DeviceOS target)
{
  if (config.habit == Habit::Custom) {
    if (config.fullShortcuts)
      return QStringLiteral("全组合快捷键需要先选择自动、Mac 习惯或 Windows/Linux 习惯；Ctrl+Space 保留。");
    return QStringLiteral("使用自定义规则，保留当前快捷键设置。");
  }
  const auto source = effectiveSourceOS(config, detected);
  if (config.habit == Habit::Automatic && source == DeviceOS::Unknown)
    return QStringLiteral("来源系统未知；请选择键鼠来源系统，或手动选择快捷键习惯；Ctrl+Space 保留。");
  if (target == DeviceOS::Unknown)
    return QStringLiteral("目标系统未知；暂不转换快捷键，Ctrl+Space 保留。");
  const auto mapping = shortcutModifierMapping(config, detected, target);
  if (!mapping)
    return config.fullShortcuts
        ? QStringLiteral("快捷键习惯与目标系统一致，直接使用原生组合键；Ctrl+Space 和其他规则保留。")
        : QStringLiteral("复制、粘贴与目标系统习惯一致，直接使用原生快捷键；Ctrl+Space 和其他规则保留。");
  if (config.fullShortcuts) {
    const auto from = mapping->source == KeyModifierSuper ? QStringLiteral("⌘") : QStringLiteral("Ctrl");
    const auto to = mapping->target == KeyModifierSuper ? QStringLiteral("⌘") : QStringLiteral("Ctrl");
    const auto scope = config.allApplications ? QStringLiteral("全组合转换应用于所有远程应用")
                                              : QStringLiteral("全组合转换仅在所选应用生效");
    return QStringLiteral("%1+… → %2+…，Ctrl+Space 保留。%3；自定义规则保留原应用范围。")
        .arg(from, to, scope);
  }
  return QStringLiteral("复制：%1 → %2；粘贴同样转换。仅在所选应用生效，Ctrl+Space 保留。")
      .arg(chordText({'c', mapping->source}, mapping->source == KeyModifierSuper ? DeviceOS::MacOS : DeviceOS::Windows),
           chordText({'c', mapping->target}, target));
}

QString validate(const Configuration &config)
{
  if (config.habit < Habit::Custom || config.habit > Habit::PC ||
      config.sourceOS < DeviceOS::Unknown || config.sourceOS > DeviceOS::Linux)
    return QStringLiteral("快捷键习惯或来源系统无效。");
  if (config.fullShortcuts && config.habit == Habit::Custom)
    return QStringLiteral("全组合快捷键需要选择自动、Mac 习惯或 Windows/Linux 习惯，不能使用自定义习惯。");
  if (const auto message = appListError(config.apps); !message.isEmpty())
    return message;
  QSet<QString> ids;
  for (const auto &rule : config.rules) {
    if (rule.id.trimmed().isEmpty() || rule.name.trimmed().isEmpty())
      return QStringLiteral("规则必须有唯一标识和名称。");
    if (ids.contains(rule.id))
      return QStringLiteral("规则标识重复：%1").arg(rule.id);
    ids.insert(rule.id);
    if (!validKey(rule.input.key) || !validKey(rule.output.key))
      return QStringLiteral("规则“%1”包含不支持的按键。").arg(rule.name);
#ifdef Q_OS_MACOS
    if (rule.output.key > kKeyF20 && rule.output.key <= kKeyF24)
      return QStringLiteral("macOS 目标按键支持 F1–F20；F21–F24 只能作为映射来源。");
#endif
    if ((rule.input.modifiers & ~kModifiers) || (rule.output.modifiers & ~kModifiers))
      return QStringLiteral("规则“%1”包含不支持的修饰键。").arg(rule.name);
    if (isReservedInput(rule.input))
      return QStringLiteral("Ctrl+Space 用于输入法切换，不能用作映射来源。");
    if (const auto message = appListError(rule.apps); !message.isEmpty())
      return message;
  }
  if (config.habit == Habit::Custom)
    return conflictError(config);
  // Automatic may see either family after reconnecting. Reject a conflict
  // before persisting, rather than disabling unrelated rules at that moment.
  for (const auto source : {DeviceOS::MacOS, DeviceOS::Windows}) {
    if (const auto message = conflictError(effectiveConfiguration(config, source)); !message.isEmpty())
      return message;
  }
  return {};
}

QString toJson(const Configuration &config)
{
  QJsonArray rules;
  for (const auto &rule : config.rules) {
    rules.push_back(QJsonObject{
        {QStringLiteral("id"), rule.id}, {QStringLiteral("name"), rule.name},
        {QStringLiteral("input"), writeChord(rule.input)}, {QStringLiteral("output"), writeChord(rule.output)},
        {QStringLiteral("enabled"), rule.enabled}, {QStringLiteral("apps"), QJsonArray::fromStringList(rule.apps)}
    });
  }
  return QString::fromUtf8(QJsonDocument(QJsonObject{
      {QStringLiteral("schema"), 4}, {QStringLiteral("enabled"), config.enabled},
      {QStringLiteral("habit"), habitName(config.habit)}, {QStringLiteral("sourceOS"), deviceOSName(config.sourceOS)},
      {QStringLiteral("fullShortcuts"), config.fullShortcuts}, {QStringLiteral("allApplications"), config.allApplications},
      {QStringLiteral("shiftTapInputMethod"), config.shiftTapInputMethod},
      {QStringLiteral("apps"), QJsonArray::fromStringList(config.apps)}, {QStringLiteral("rules"), rules}
  }).toJson(QJsonDocument::Compact));
}

std::optional<Configuration> fromJson(const QString &json, QString *error)
{
  setError(error, {});
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
  const auto fail = [error](const QString &message) -> std::optional<Configuration> {
    setError(error, message);
    return std::nullopt;
  };
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
    return fail(QStringLiteral("快捷键配置不是有效的 JSON 对象。"));
  const auto object = document.object();
  const auto schema = integer(object.value(QStringLiteral("schema")));
  if (!schema || (*schema < 1 || *schema > 4))
    return fail(QStringLiteral("不支持此快捷键配置版本。"));
  auto fields = QStringList{QStringLiteral("schema"), QStringLiteral("enabled"), QStringLiteral("apps"), QStringLiteral("rules")};
  if (*schema >= 2)
    fields << QStringLiteral("habit") << QStringLiteral("sourceOS");
  if (*schema >= 3)
    fields << QStringLiteral("fullShortcuts") << QStringLiteral("allApplications");
  if (*schema >= 4)
    fields << QStringLiteral("shiftTapInputMethod");
  if (!exactFields(object, fields))
    return fail(QStringLiteral("快捷键配置字段缺失或包含未知字段。"));
  const auto apps = stringList(object.value(QStringLiteral("apps")));
  if (!object.value(QStringLiteral("enabled")).isBool() || !apps ||
      !object.value(QStringLiteral("rules")).isArray())
    return fail(QStringLiteral("快捷键配置字段类型不正确。"));
  Configuration config;
  config.enabled = object.value(QStringLiteral("enabled")).toBool();
  config.apps = *apps;
  if (*schema >= 2) {
    const auto habit = object.value(QStringLiteral("habit"));
    const auto source = object.value(QStringLiteral("sourceOS"));
    if (!habit.isString() || !source.isString())
      return fail(QStringLiteral("快捷键习惯和来源系统必须是字符串。"));
    const QStringList habits{QStringLiteral("custom"), QStringLiteral("automatic"), QStringLiteral("mac"), QStringLiteral("pc")};
    const QStringList systems{QStringLiteral("unknown"), QStringLiteral("macos"), QStringLiteral("windows"), QStringLiteral("linux")};
    const auto h = habits.indexOf(habit.toString());
    const auto s = systems.indexOf(source.toString());
    if (h < 0 || s < 0)
      return fail(QStringLiteral("快捷键习惯或来源系统不受支持。"));
    config.habit = static_cast<Habit>(h);
    config.sourceOS = static_cast<DeviceOS>(s);
  }
  if (*schema >= 3) {
    const auto full = object.value(QStringLiteral("fullShortcuts"));
    const auto all = object.value(QStringLiteral("allApplications"));
    if (!full.isBool() || !all.isBool())
      return fail(QStringLiteral("全组合快捷键和所有应用选项必须是布尔值。"));
    config.fullShortcuts = full.toBool();
    config.allApplications = all.toBool();
  }
  if (*schema >= 4) {
    const auto shiftTap = object.value(QStringLiteral("shiftTapInputMethod"));
    if (!shiftTap.isBool())
      return fail(QStringLiteral("轻按 Shift 切换输入法选项必须是布尔值。"));
    config.shiftTapInputMethod = shiftTap.toBool();
  }
  for (const auto &item : object.value(QStringLiteral("rules")).toArray()) {
    if (!item.isObject())
      return fail(QStringLiteral("规则必须是 JSON 对象。"));
    const auto rule = item.toObject();
    if (!exactFields(rule, {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("input"),
                            QStringLiteral("output"), QStringLiteral("enabled"), QStringLiteral("apps")}))
      return fail(QStringLiteral("规则字段缺失或包含未知字段。"));
    const auto input = readChord(rule.value(QStringLiteral("input")));
    const auto output = readChord(rule.value(QStringLiteral("output")));
    const auto ruleApps = stringList(rule.value(QStringLiteral("apps")));
    if (!rule.value(QStringLiteral("id")).isString() || !rule.value(QStringLiteral("name")).isString() ||
        !rule.value(QStringLiteral("enabled")).isBool() || !input || !output || !ruleApps)
      return fail(QStringLiteral("规则字段类型不正确。"));
    config.rules.push_back({rule.value(QStringLiteral("id")).toString(), rule.value(QStringLiteral("name")).toString(),
                            *input, *output, rule.value(QStringLiteral("enabled")).toBool(), *ruleApps});
  }
  if (const auto message = validate(config); !message.isEmpty())
    return fail(message);
  return config;
}

Configuration load(QString *error)
{
  setError(error, {});
  const auto raw = Settings::value(Settings::Client::ShortcutConfig).toString();
  if (raw.trimmed().isEmpty()) {
    Configuration config;
    config.enabled = Settings::value(Settings::Client::ClipboardShortcuts).toBool();
    config.apps = Settings::value(Settings::Client::ClipboardShortcutApps).toStringList();
    config.rules = defaultRules(config.apps);
    if (const auto message = validate(config); !message.isEmpty()) {
      setError(error, message);
      return closedConfiguration();
    }
    return config;
  }
  const auto parsed = fromJson(raw, error);
  return parsed ? *parsed : closedConfiguration();
}

bool save(const Configuration &config, QString *error)
{
  const auto message = validate(config);
  setError(error, message);
  if (!message.isEmpty())
    return false;
  return Settings::trySetValue(Settings::Client::ShortcutConfig, toJson(config), error);
}

QString chordText(const Chord &chord, DeviceOS os)
{
  QStringList parts;
  if (chord.modifiers & KeyModifierControl)
    parts << QStringLiteral("Ctrl");
  if (chord.modifiers & KeyModifierShift)
    parts << QStringLiteral("Shift");
  if (chord.modifiers & KeyModifierAlt)
    parts << QStringLiteral("Alt");
  if (chord.modifiers & KeyModifierSuper)
    parts << (os == DeviceOS::MacOS ? QStringLiteral("⌘") :
              os == DeviceOS::Windows ? QStringLiteral("Win") : QStringLiteral("Super"));
  QString key;
  if (chord.key >= kKeyF1 && chord.key <= kKeyF24) {
    key = QStringLiteral("F%1").arg(chord.key - kKeyF1 + 1);
  } else if (chord.key > ' ' && chord.key <= '~') {
    key = QString(QChar::fromLatin1(static_cast<char>(chord.key))).toUpper();
  } else {
    switch (chord.key) {
    case ' ': key = QStringLiteral("Space"); break;
    case kKeyBackSpace: key = QStringLiteral("⌫"); break;
    case kKeyTab: key = QStringLiteral("Tab"); break;
    case kKeyReturn: key = QStringLiteral("Return"); break;
    case kKeyEscape: key = QStringLiteral("Esc"); break;
    case kKeyDelete: key = QStringLiteral("Delete"); break;
    case kKeyHome: key = QStringLiteral("Home"); break;
    case kKeyLeft: key = QStringLiteral("←"); break;
    case kKeyUp: key = QStringLiteral("↑"); break;
    case kKeyRight: key = QStringLiteral("→"); break;
    case kKeyDown: key = QStringLiteral("↓"); break;
    case kKeyPageUp: key = QStringLiteral("Page Up"); break;
    case kKeyPageDown: key = QStringLiteral("Page Down"); break;
    case kKeyEnd: key = QStringLiteral("End"); break;
    case kKeyInsert: key = QStringLiteral("Insert"); break;
    default: key = QStringLiteral("未设置"); break;
    }
  }
  parts << key;
  return parts.join('+');
}

} // namespace deskflow::shortcuts
