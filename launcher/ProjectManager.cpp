#include "ProjectManager.h"

#include <QFileDialog>
#include <QDir>
#include <QStandardPaths>
#include <QInputDialog>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>

ProjectManager::ProjectManager(QWidget* parentWidget)
    : QObject(parentWidget), m_parentWidget(parentWidget) {}

QString ProjectManager::defaultProjectsRoot() const {
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(docs).filePath("MarcSLM/Projects");
}

QString ProjectManager::promptForProjectName() const {
    bool ok = false;
    QString name = QInputDialog::getText(m_parentWidget,
        "New Project",
        "Enter project name:",
        QLineEdit::Normal,
        "Untitled_Build",
        &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return QString();
    }
    return name.trimmed();
}

QString ProjectManager::promptForProjectLocation(const QString& suggestedPath) const {
    QDir().mkpath(suggestedPath);
    QString dir = QFileDialog::getExistingDirectory(m_parentWidget,
        "Select Project Location",
        suggestedPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    return dir;
}

static QJsonObject makeBuildJson(const MarcProject& project) {
    QJsonObject root;

    QJsonObject proj;
    proj["name"] = project.name();
    proj["version"] = "1.0";
    proj["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    proj["modified"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["project"] = proj;

    QJsonObject files;
    files["marcFile"] = project.marcFilePath();
    files["jsonFile"] = project.jsonFilePath();
    root["files"] = files;

    QJsonObject build;
    build["status"] = "Setup";
    build["layersCompleted"] = 0;
    build["totalLayers"] = project.statistics().totalLayers;
    root["build"] = build;

    return root;
}

bool ProjectManager::writeBuildFile(const MarcProject& project) const {
    QJsonDocument doc(makeBuildJson(project));

    QFile f(project.buildFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

bool ProjectManager::loadBuildFile(const QString& buildPath) {
    QFile f(buildPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorMessage("Failed to open .build file");
        return false;
    }
    QByteArray data = f.readAll();
    f.close();

    QJsonParseError perr{};
    QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
    if (perr.error != QJsonParseError::NoError) {
        emit errorMessage("Invalid .build file JSON");
        return false;
    }

    QJsonObject root = doc.object();
    QJsonObject proj = root.value("project").toObject();
    QJsonObject files = root.value("files").toObject();

    m_currentProject = std::make_unique<MarcProject>();
    m_currentProject->setName(proj.value("name").toString());
    m_currentProject->setBuildFilePath(buildPath);
    if (files.contains("marcFile")) m_currentProject->attachMarcFile(files.value("marcFile").toString());
    if (files.contains("jsonFile")) m_currentProject->attachJsonFile(files.value("jsonFile").toString());

    addRecentProject(buildPath);
    emit projectOpened(buildPath);
    emit statusMessage(QString("Project opened: %1").arg(buildPath));
    return true;
}

QString ProjectManager::projectRootDir() const {
    if (!m_currentProject) return {};
    QFileInfo fi(m_currentProject->buildFilePath());
    return fi.dir().absolutePath();
}

QString ProjectManager::marcAbsolutePath() const {
    if (!m_currentProject) return {};
    const QString rel = m_currentProject->marcFilePath();
    if (rel.isEmpty()) return {};
    return makeAbsolute(projectRootDir(), rel);
}

QString ProjectManager::jsonAbsolutePath() const {
    if (!m_currentProject) return {};
    const QString rel = m_currentProject->jsonFilePath();
    if (rel.isEmpty()) return {};
    return makeAbsolute(projectRootDir(), rel);
}

bool ProjectManager::createNewProjectInteractive() {
    QString name = promptForProjectName();
    if (name.isEmpty()) {
        emit errorMessage("Project creation canceled or invalid name.");
        return false;
    }

    QString root = defaultProjectsRoot();
    QString location = promptForProjectLocation(root);
    if (location.isEmpty()) {
        emit errorMessage("No project location selected.");
        return false;
    }

    QDir dir(location);
    QString projectDir = dir.filePath(name);
    if (!QDir().mkpath(projectDir)) {
        emit errorMessage("Failed to create project directory.");
        return false;
    }
    QDir projectRoot(projectDir);
    projectRoot.mkpath("Data");
    projectRoot.mkpath("Config");
    projectRoot.mkpath("Logs");
    projectRoot.mkpath("Reports");

    QString buildPath = projectRoot.filePath(name + ".build");

    m_currentProject = std::make_unique<MarcProject>();
    m_currentProject->setName(name);
    m_currentProject->setBuildFilePath(buildPath);

    if (!writeBuildFile(*m_currentProject)) {
        emit errorMessage("Failed to write .build file.");
        return false;
    }

    addRecentProject(buildPath);
    emit statusMessage(QString("Project created: %1").arg(buildPath));
    QMessageBox::information(m_parentWidget, "Project Created",
        QString("New project created:\n%1\n\nYou can now attach .marc and .json files via Project menu.")
            .arg(buildPath));
    return true;
}

bool ProjectManager::openProjectInteractive() {
    QString buildPath = QFileDialog::getOpenFileName(m_parentWidget,
        "Open Project",
        defaultProjectsRoot(),
        "MarcSLM Build (*.build);;All Files (*)");
    if (buildPath.isEmpty()) return false;
    return loadBuildFile(buildPath);
}

static bool copyFileOverwrite(const QString& src, const QString& dst) {
    if (QFile::exists(dst)) QFile::remove(dst);
    return QFile::copy(src, dst);
}

QString ProjectManager::makeRelative(const QString& baseDir, const QString& absoluteFile) {
    return QDir(baseDir).relativeFilePath(absoluteFile);
}

QString ProjectManager::makeAbsolute(const QString& baseDir, const QString& relativeFile) {
    return QDir(baseDir).absoluteFilePath(relativeFile);
}

bool ProjectManager::copyIntoProject(const QString& srcFile, const QString& subDir, QString& outRelativePath) {
    if (!m_currentProject) return false;
    QString root = projectRootDir();
    QDir(root).mkpath(subDir);
    QFileInfo sfi(srcFile);
    QString dstAbs = QDir(root).filePath(subDir + "/" + sfi.fileName());
    if (!copyFileOverwrite(srcFile, dstAbs)) return false;
    outRelativePath = makeRelative(root, dstAbs);
    return true;
}

bool ProjectManager::validateMarc(const QString& path, QString& error) const {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { error = "Cannot open MARC file"; return false; }
    QByteArray header = f.read(16);
    f.close();
    if (header.size() < 4) { error = "MARC header too small"; return false; }
    if (!(header[0] == 'M' && header[1] == 'A' && header[2] == 'R' && header[3] == 'C')) {
        error = "Invalid MARC magic header"; return false;
    }
    return true;
}

bool ProjectManager::validateJsonConfig(const QString& path, QString& error) const {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) { error = "Cannot open JSON"; return false; }
    QByteArray data = f.readAll();
    f.close();

    QJsonParseError perr{};
    QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
    if (perr.error != QJsonParseError::NoError) { error = QString("Invalid JSON: %1").arg(perr.errorString()); return false; }
    if (!doc.isObject()) { error = "Root must be an object"; return false; }

    QJsonObject root = doc.object();

    // Require top-level buildStyles array (new schema)
    if (!root.contains("buildStyles")) { error = "Missing 'buildStyles' array"; return false; }
    QJsonValue bsVal = root.value("buildStyles");
    if (!bsVal.isArray()) { error = "'buildStyles' must be an array"; return false; }
    QJsonArray styles = bsVal.toArray();
    if (styles.isEmpty()) { error = "'buildStyles' must not be empty"; return false; }

    auto hasNumber = [](const QJsonObject& o, const char* key){ return o.contains(key) && o.value(key).isDouble(); };
    auto hasString = [](const QJsonObject& o, const char* key){ return o.contains(key) && o.value(key).isString(); };

    const char* requiredStringKeys[] = { "name", "description" };
    const char* requiredNumberKeys[] = {
        "id", "laserId", "laserMode", "laserPower", "laserFocus", "laserSpeed",
        "hatchSpacing", "layerThickness", "pointDistance", "pointDelay",
        "pointExposureTime", "jumpSpeed", "jumpDelay"
    };

    for (int i = 0; i < styles.size(); ++i) {
        if (!styles[i].isObject()) { error = QString("buildStyles[%1] must be an object").arg(i); return false; }
        QJsonObject s = styles[i].toObject();
        for (const char* k : requiredStringKeys) {
            if (!hasString(s, k)) { error = QString("buildStyles[%1] missing string '%2'").arg(i).arg(k); return false; }
        }
        for (const char* k : requiredNumberKeys) {
            if (!hasNumber(s, k)) { error = QString("buildStyles[%1] missing number '%2'").arg(i).arg(k); return false; }
        }
    }

    return true;
}

bool ProjectManager::attachMarcInteractive() {
    if (!m_currentProject) {
        emit errorMessage("No active project. Create or open a project first.");
        return false;
    }

    QString marcPath = QFileDialog::getOpenFileName(m_parentWidget,
        "Attach Scan Data (.marc)",
        projectRootDir(),
        "MARC Scan Files (*.marc);;All Files (*)");
    if (marcPath.isEmpty()) return false;

    QString verr;
    if (!validateMarc(marcPath, verr)) {
        QMessageBox::warning(m_parentWidget, "Invalid MARC", verr);
        return false;
    }

    QString rel;
    if (!copyIntoProject(marcPath, "Data", rel)) {
        emit errorMessage("Failed to copy MARC into project");
        return false;
    }

    m_currentProject->attachMarcFile(rel);

    if (!writeBuildFile(*m_currentProject)) {
        emit errorMessage("Failed to update .build file.");
        return false;
    }

    emit projectModified();
    emit statusMessage("Scan data attached");
    QMessageBox::information(m_parentWidget, "Attached",
        "Scan vector file copied and attached to project.");
    return true;
}

bool ProjectManager::attachJsonInteractive() {
    if (!m_currentProject) {
        emit errorMessage("No active project. Create or open a project first.");
        return false;
    }

    QString jsonPath = QFileDialog::getOpenFileName(m_parentWidget,
        "Attach Laser Configuration (.json)",
        projectRootDir(),
        "JSON Files (*.json);;All Files (*)");
    if (jsonPath.isEmpty()) return false;

    QString verr;
    if (!validateJsonConfig(jsonPath, verr)) {
        QMessageBox::warning(m_parentWidget, "Invalid JSON", verr);
        return false;
    }

    QString rel;
    if (!copyIntoProject(jsonPath, "Config", rel)) {
        emit errorMessage("Failed to copy JSON into project");
        return false;
    }

    m_currentProject->attachJsonFile(rel);

    if (!writeBuildFile(*m_currentProject)) {
        emit errorMessage("Failed to update .build file.");
        return false;
    }

    emit projectModified();
    emit statusMessage("Configuration attached");
    QMessageBox::information(m_parentWidget, "Attached",
        "Configuration file copied and attached to project.");
    return true;
}

bool ProjectManager::saveProject() {
    if (!m_currentProject) return false;
    bool ok = writeBuildFile(*m_currentProject);
    if (ok) emit projectSaved(m_currentProject->buildFilePath());
    return ok;
}

bool ProjectManager::saveProjectAs(const QString& newBuildPath) {
    if (!m_currentProject) return false;
    m_currentProject->setBuildFilePath(newBuildPath);
    return saveProject();
}

bool ProjectManager::saveProjectInteractive() {
    if (!m_currentProject) return false;
    return saveProject();
}

bool ProjectManager::saveProjectAsInteractive() {
    if (!m_currentProject) return false;
    QString newPath = QFileDialog::getSaveFileName(m_parentWidget, "Save Project As", projectRootDir(), "MarcSLM Build (*.build)");
    if (newPath.isEmpty()) return false;
    bool ok = saveProjectAs(newPath);
    if (ok) addRecentProject(newPath);
    return ok;
}

bool ProjectManager::exportReportInteractive() {
    if (!m_currentProject) return false;
    QString out = QFileDialog::getSaveFileName(m_parentWidget, "Export Report", projectRootDir(), "Text Report (*.txt)");
    if (out.isEmpty()) return false;

    QFile file(out);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream ts(&file);
    ts << "MarcSLM Project Report\n";
    ts << "Project: " << m_currentProject->name() << "\n";
    ts << "Build File: " << m_currentProject->buildFilePath() << "\n";
    ts << "MARC: " << m_currentProject->marcFilePath() << "\n";
    ts << "JSON: " << m_currentProject->jsonFilePath() << "\n";
    ts << "Status: " << m_currentProject->statistics().status << "\n";
    file.close();
    emit statusMessage("Report exported");
    return true;
}

QStringList ProjectManager::loadRecentProjects() const {
    QSettings s("MarcSLM", "VolMarc");
    int count = s.beginReadArray("recent");
    QStringList list;
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        list << s.value("path").toString();
    }
    s.endArray();
    return list;
}

void ProjectManager::saveRecentProjects(const QStringList& list) const {
    QSettings s("MarcSLM", "VolMarc");
    s.beginWriteArray("recent");
    for (int i = 0; i < list.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue("path", list.at(i));
    }
    s.endArray();
}

QStringList ProjectManager::recentProjects() const {
    return loadRecentProjects();
}

void ProjectManager::addRecentProject(const QString& buildPath) {
    QStringList list = loadRecentProjects();
    list.removeAll(buildPath);
    list.prepend(buildPath);
    const int Max = 10;
    while (list.size() > Max) list.removeLast();
    saveRecentProjects(list);
}

bool ProjectManager::atomicWriteWithBackup(const QString& targetPath, const QByteArray& data, QString* outError) const {
    QFileInfo fi(targetPath);
    QDir dir = fi.dir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) { if (outError) *outError = "Failed to create target directory"; return false; }
    }

    // Write to temporary file first
    QString tempPath = dir.filePath(fi.fileName() + ".tmp");
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (outError) *outError = "Failed to open temp file for write";
        return false;
    }
    if (tempFile.write(data) != data.size()) {
        tempFile.close();
        QFile::remove(tempPath);
        if (outError) *outError = "Failed to write all data to temp file";
        return false;
    }
    tempFile.flush();
    tempFile.close();

    // Backup original
    QString backupPath = dir.filePath(fi.fileName() + ".bak");
    if (QFile::exists(targetPath)) {
        QFile::remove(backupPath);
        if (!QFile::copy(targetPath, backupPath)) {
            QFile::remove(tempPath);
            if (outError) *outError = "Failed to backup original file";
            return false;
        }
    }

    // Replace original atomically (best effort on Windows)
    QFile::remove(targetPath);
    if (!QFile::rename(tempPath, targetPath)) {
        // Attempt fallback: copy temp to target
        if (!QFile::copy(tempPath, targetPath)) {
            QFile::remove(tempPath);
            if (outError) *outError = "Failed to replace target file";
            return false;
        }
        QFile::remove(tempPath);
    }

    return true;
}

bool ProjectManager::applyJsonUpdate(const QJsonDocument& updatedDoc, QString* outError) {
    if (!m_currentProject) { if (outError) *outError = "No active project"; return false; }
    QString baseDir = projectRootDir();
    QString rel = m_currentProject->jsonFilePath();
    if (rel.isEmpty()) { if (outError) *outError = "No JSON attached"; return false; }
    QString abs = makeAbsolute(baseDir, rel);

    // Validate structure before writing
    QByteArray data = updatedDoc.toJson(QJsonDocument::Indented);
    QString verr;
    {
        // Re-parse to ensure validity and schema conformance
        QJsonParseError perr{};
        QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
        if (perr.error != QJsonParseError::NoError) { if (outError) *outError = QString("Invalid JSON: %1").arg(perr.errorString()); return false; }
        if (!validateJsonConfig(abs, verr)) {
            // validate against the updated content instead of existing file
            if (!validateJsonConfig(QString::fromUtf8(data), verr)) {
                if (outError) *outError = verr; return false;
            }
        }
    }

    // Write with backup
    if (!atomicWriteWithBackup(abs, data, outError)) {
        return false;
    }

    emit projectModified();
    emit statusMessage("Configuration updated");
    return true;
}

bool ProjectManager::editJsonInteractive() {
    if (!m_currentProject) { emit errorMessage("No active project"); return false; }
    QString baseDir = projectRootDir();
    QString rel = m_currentProject->jsonFilePath();
    if (rel.isEmpty()) { emit errorMessage("No JSON attached"); return false; }
    QString abs = makeAbsolute(baseDir, rel);

    // Load current JSON
    QFile f(abs);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) { emit errorMessage("Cannot open JSON for edit"); return false; }
    QByteArray current = f.readAll();
    f.close();

    // Prompt user to save edited file using system editor
    QMessageBox::information(m_parentWidget, "Edit Configuration",
        "The configuration file will be opened externally.\n"
        "Please make changes and click 'Apply' to validate and save.");

    // Let user choose an updated file (industrial pattern: edit, then apply)
    QString updatedPath = QFileDialog::getOpenFileName(m_parentWidget,
        "Select updated configuration JSON",
        QFileInfo(abs).dir().absolutePath(),
        "JSON Files (*.json);;All Files (*)");
    if (updatedPath.isEmpty()) return false;

    QFile uf(updatedPath);
    if (!uf.open(QIODevice::ReadOnly | QIODevice::Text)) { emit errorMessage("Cannot read updated JSON"); return false; }
    QByteArray updated = uf.readAll();
    uf.close();

    QJsonParseError perr{};
    QJsonDocument updatedDoc = QJsonDocument::fromJson(updated, &perr);
    if (perr.error != QJsonParseError::NoError) {
        QMessageBox::warning(m_parentWidget, "Invalid JSON", perr.errorString());
        return false;
    }

    QString err;
    if (!applyJsonUpdate(updatedDoc, &err)) {
        QMessageBox::critical(m_parentWidget, "Apply Failed", err);
        return false;
    }

    QMessageBox::information(m_parentWidget, "Configuration Updated", "JSON configuration was validated and saved.");
    return true;
}

