// ProjectManager.h

#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QDateTime>
#include <memory>

class QWidget;

struct LaserConfig {
    // Identification
    int id = 0;
    QString name;
    QString description;

    // Laser and process parameters
    int laserId = 0;
    int laserMode = 0;
    double laserPower = 0.0;
    double laserFocus = 0.0;
    double laserSpeed = 0.0;

    // Hatch and layer geometry
    double hatchSpacing = 0.0;
    double layerThickness = 0.0;

    // Point-based settings
    double pointDistance = 0.0;
    int pointDelay = 0;
    int pointExposureTime = 0;

    // Jump settings
    double jumpSpeed = 0.0;
    double jumpDelay = 0.0;

    // Legacy/defaults retained for backward compatibility
    double minPower = 0.0;
    double maxPower = 4095.0;
    double defaultPower = 2048.0;
    double markSpeed = 250.0;
    double jumpSpeedLegacy = 1000.0;
    int laserOnDelay = 100;
    int laserOffDelay = 100;
    int jumpDelayLegacy = 250;
    int markDelay = 100;
    int polygonDelay = 50;
    bool wobbleEnabled = false;
    double wobbleAmplitude = 50.0;
    double wobbleFrequency = 100.0;
};

struct LayerInfo {
    int layerNumber = 0;
    int vectorCount = 0;
    qint64 fileOffset = 0;
    double layerThickness = 0.05;
    QString layerType = "Normal";
};

struct BuildStatistics {
    int totalLayers = 0;
    int layersCompleted = 0;
    int totalVectors = 0;
    double estimatedBuildTime = 0.0;
    double actualBuildTime = 0.0;
    QDateTime startTime;
    QDateTime endTime;
    QString status = "Not Started";
};

class MarcProject : public QObject {
    Q_OBJECT
public:
    explicit MarcProject(QObject* parent = nullptr) : QObject(parent) {}
    ~MarcProject() override {}

    QString name() const { return m_name; }
    QString buildFilePath() const { return m_buildFilePath; }
    QString marcFilePath() const { return m_marcFilePath; }
    QString jsonFilePath() const { return m_jsonFilePath; }

    void setName(const QString& n) { m_name = n; }
    void setBuildFilePath(const QString& p) { m_buildFilePath = p; }

    bool attachMarcFile(const QString& path) { m_marcFilePath = path; emit modified(); return true; }
    bool attachJsonFile(const QString& path) { m_jsonFilePath = path; emit modified(); return true; }

    bool isValid() const { return !m_name.isEmpty() && !m_buildFilePath.isEmpty(); }

    const LaserConfig& laserConfig() const { return m_laserConfig; }
    LaserConfig& laserConfig() { return m_laserConfig; }

    const QList<LayerInfo>& layers() const { return m_layers; }
    QList<LayerInfo>& layers() { return m_layers; }

    const BuildStatistics& statistics() const { return m_stats; }
    BuildStatistics& statistics() { return m_stats; }

signals:
    void modified();

private:
    QString m_name;
    QString m_buildFilePath;
    QString m_marcFilePath;
    QString m_jsonFilePath;

    LaserConfig m_laserConfig;
    QList<LayerInfo> m_layers;
    BuildStatistics m_stats;
};

class ProjectManager : public QObject {
    Q_OBJECT
public:
    explicit ProjectManager(QWidget* parentWidget = nullptr);

    // Interactive flows
    bool createNewProjectInteractive();
    bool openProjectInteractive();
    bool attachMarcInteractive();
    bool attachJsonInteractive();
    bool saveProjectInteractive();
    bool saveProjectAsInteractive();
    bool exportReportInteractive();
    bool editJsonInteractive();

    // Non-interactive API
    bool saveProject();
    bool saveProjectAs(const QString& newBuildPath);
    bool applyJsonUpdate(const QJsonDocument& updatedDoc, QString* outError = nullptr);

    // State
    MarcProject* currentProject() const { return m_currentProject.get(); }
    bool hasProject() const { return m_currentProject != nullptr; }

    // Recent
    QStringList recentProjects() const;
    void addRecentProject(const QString& buildPath);

    // Resolved paths
    QString marcAbsolutePath() const;
    QString jsonAbsolutePath() const;

signals:
    void statusMessage(const QString& message);
    void errorMessage(const QString& message);
    void projectOpened(const QString& buildPath);
    void projectSaved(const QString& buildPath);
    void projectModified();

private:
    QWidget* m_parentWidget;
    std::unique_ptr<MarcProject> m_currentProject;

    QString defaultProjectsRoot() const;
    QString promptForProjectName() const;
    QString promptForProjectLocation(const QString& suggestedPath) const;
    bool writeBuildFile(const MarcProject& project) const;

    // Helpers
    bool loadBuildFile(const QString& buildPath);
    QString projectRootDir() const;

    // Copy helpers
    bool copyIntoProject(const QString& srcFile, const QString& subDir, QString& outRelativePath);
    static QString makeRelative(const QString& baseDir, const QString& absoluteFile);
    static QString makeAbsolute(const QString& baseDir, const QString& relativeFile);

    // Validation
    bool validateMarc(const QString& path, QString& error) const;
    bool validateJsonConfig(const QString& path, QString& error) const;

    // Atomic write helper
    bool atomicWriteWithBackup(const QString& targetPath, const QByteArray& data, QString* outError = nullptr) const;

    // Settings for recent projects
    QStringList loadRecentProjects() const;
    void saveRecentProjects(const QStringList& list) const;
};

#endif // PROJECTMANAGER_H
