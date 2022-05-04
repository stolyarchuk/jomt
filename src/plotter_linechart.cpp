// Copyright 2019 Guillaume AUJAY. All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "plotter_linechart.h"

#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QtCharts>

#include "benchmark_results.h"
#include "result_parser.h"
#include "ui_plotter_linechart.h"

PlotterLineChart::PlotterLineChart(const BenchResults& bchResults, const QVector<int>& bchIdxs,
                                   const PlotParams& plotParams, const QString& origFilename,
                                   const QVector<FileReload>& addFilenames, QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::PlotterLineChart)
    , mBenchIdxs(bchIdxs)
    , mPlotParams(plotParams)
    , mOrigFilename(origFilename)
    , mAddFilenames(addFilenames)
    , mAllIndexes(bchIdxs.size() == bchResults.benchmarks.size())
    , mWatcher(parent) {
  // UI
  ui->setupUi(this);
  this->setAttribute(Qt::WA_DeleteOnClose);

  QFileInfo fileInfo(origFilename);
  QString chartType = (plotParams.type == ChartLineType) ? "Lines - " : "Splines - ";
  this->setWindowTitle(chartType + fileInfo.fileName());

  connectUI();

  // TODO: select points
  // See: https://doc.qt.io/qt-5/qtcharts-callout-example.html

  // Init
  setupChart(bchResults, bchIdxs, plotParams);
  setupOptions();

  // Show
  ui->horizontalLayout->insertWidget(0, mChartView);
}

PlotterLineChart::~PlotterLineChart() {
  // Save options to file
  saveConfig();

  delete ui;
}

void PlotterLineChart::connectUI() {
  // Theme
  ui->comboBoxTheme->addItem("Light", QChart::ChartThemeLight);
  ui->comboBoxTheme->addItem("Blue Cerulean", QChart::ChartThemeBlueCerulean);
  ui->comboBoxTheme->addItem("Dark", QChart::ChartThemeDark);
  ui->comboBoxTheme->addItem("Brown Sand", QChart::ChartThemeBrownSand);
  ui->comboBoxTheme->addItem("Blue Ncs", QChart::ChartThemeBlueNcs);
  ui->comboBoxTheme->addItem("High Contrast", QChart::ChartThemeHighContrast);
  ui->comboBoxTheme->addItem("Blue Icy", QChart::ChartThemeBlueIcy);
  ui->comboBoxTheme->addItem("Qt", QChart::ChartThemeQt);
  connect(ui->comboBoxTheme, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &PlotterLineChart::onComboThemeChanged);

  // Legend
  connect(ui->checkBoxLegendVisible, &QCheckBox::stateChanged, this,
          &PlotterLineChart::onCheckLegendVisible);

  ui->comboBoxLegendAlign->addItem("Top", Qt::AlignTop);
  ui->comboBoxLegendAlign->addItem("Bottom", Qt::AlignBottom);
  ui->comboBoxLegendAlign->addItem("Left", Qt::AlignLeft);
  ui->comboBoxLegendAlign->addItem("Right", Qt::AlignRight);
  connect(ui->comboBoxLegendAlign, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &PlotterLineChart::onComboLegendAlignChanged);

  connect(ui->spinBoxLegendFontSize, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &PlotterLineChart::onSpinLegendFontSizeChanged);
  connect(ui->pushButtonSeries, &QPushButton::clicked, this,
          &PlotterLineChart::onSeriesEditClicked);

  if (!isYTimeBased(mPlotParams.yType))
    ui->comboBoxTimeUnit->setEnabled(false);
  else {
    ui->comboBoxTimeUnit->addItem("ns", 1000.);
    ui->comboBoxTimeUnit->addItem("us", 1.);
    ui->comboBoxTimeUnit->addItem("ms", 0.001);
    connect(ui->comboBoxTimeUnit, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PlotterLineChart::onComboTimeUnitChanged);
  }

  // Axes
  ui->comboBoxAxis->addItem("X-Axis");
  ui->comboBoxAxis->addItem("Y-Axis");
  connect(ui->comboBoxAxis, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &PlotterLineChart::onComboAxisChanged);

  connect(ui->checkBoxAxisVisible, &QCheckBox::stateChanged, this,
          &PlotterLineChart::onCheckAxisVisible);
  connect(ui->checkBoxTitle, &QCheckBox::stateChanged, this,
          &PlotterLineChart::onCheckTitleVisible);
  connect(ui->checkBoxLog, &QCheckBox::stateChanged, this, &PlotterLineChart::onCheckLog);
  connect(ui->spinBoxLogBase, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &PlotterLineChart::onSpinLogBaseChanged);
  connect(ui->lineEditTitle, &QLineEdit::textChanged, this, &PlotterLineChart::onEditTitleChanged);
  connect(ui->spinBoxTitleSize, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &PlotterLineChart::onSpinTitleSizeChanged);
  connect(ui->lineEditFormat, &QLineEdit::textChanged, this,
          &PlotterLineChart::onEditFormatChanged);
  connect(ui->spinBoxLabelSize, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &PlotterLineChart::onSpinLabelSizeChanged);
  connect(ui->doubleSpinBoxMin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          &PlotterLineChart::onSpinMinChanged);
  connect(ui->doubleSpinBoxMax, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          &PlotterLineChart::onSpinMaxChanged);
  connect(ui->spinBoxTicks, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &PlotterLineChart::onSpinTicksChanged);
  connect(ui->spinBoxMTicks, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &PlotterLineChart::onSpinMTicksChanged);

  // Actions
  connect(&mWatcher, &QFileSystemWatcher::fileChanged, this, &PlotterLineChart::onAutoReload);
  connect(ui->checkBoxAutoReload, &QCheckBox::stateChanged, this,
          &PlotterLineChart::onCheckAutoReload);
  connect(ui->pushButtonReload, &QPushButton::clicked, this, &PlotterLineChart::onReloadClicked);
  connect(ui->pushButtonSnapshot, &QPushButton::clicked, this,
          &PlotterLineChart::onSnapshotClicked);
}

void PlotterLineChart::setupChart(const BenchResults& bchResults, const QVector<int>& bchIdxs,
                                  const PlotParams& plotParams, bool init) {
  QScopedPointer<QChart> scopedChart;
  QChart* chart = nullptr;
  if (init) {
    scopedChart.reset(new QChart());
    chart = scopedChart.get();
  } else {  // Re-init
    chart = mChartView->chart();
    chart->setTitle("");
    chart->removeAllSeries();
    const auto xAxes = chart->axes(Qt::Horizontal);
    if (!xAxes.empty())
      chart->removeAxis(xAxes.constFirst());
    const auto yAxes = chart->axes(Qt::Vertical);
    if (!yAxes.empty())
      chart->removeAxis(yAxes.constFirst());
    mSeriesMapping.clear();
  }
  Q_ASSERT(chart);

  // Time unit
  mCurrentTimeFactor = 1.;
  if (isYTimeBased(mPlotParams.yType)) {
    if (bchResults.meta.time_unit == "ns")
      mCurrentTimeFactor = 1000.;
    else if (bchResults.meta.time_unit == "ms")
      mCurrentTimeFactor = 0.001;
  }

  // 2D Lines
  // X: argumentA or templateB
  // Y: time/iter/bytes/items (not name dependent)
  // Line: one per benchmark % X-param
  QVector<BenchSubset> bchSubsets =
      bchResults.groupParam(plotParams.xType == PlotArgumentType, bchIdxs, plotParams.xIdx, "X");
  bool custDataAxis = true;
  QString custDataName;
  for (const auto& bchSubset : qAsConst(bchSubsets)) {
    // Ignore single point lines
    if (bchSubset.idxs.size() < 2) {
      qWarning() << "Not enough points to trace line for: " << bchSubset.name;
      continue;
    }

    // Chart type
    QScopedPointer<QLineSeries> series;
    if (plotParams.type == ChartLineType)
      series.reset(new QLineSeries());
    else
      series.reset(new QSplineSeries());

    const QString& subsetName = bchSubset.name;
    //        qDebug() << "subsetName:" << subsetName;
    //        qDebug() << "subsetIdxs:" << bchSubset.idxs;

    double xFallback = 0.;
    for (int idx : bchSubset.idxs) {
      QString xName =
          bchResults.getParamName(plotParams.xType == PlotArgumentType, idx, plotParams.xIdx);
      double xVal = BenchResults::getParamValue(xName, custDataName, custDataAxis, xFallback);

      // Add point
      series->append(
          xVal, getYPlotValue(bchResults.benchmarks[idx], plotParams.yType) * mCurrentTimeFactor);
    }
    // Add series
    series->setName(subsetName.toHtmlEscaped());
    mSeriesMapping.push_back({subsetName, subsetName});  // color set later
    chart->addSeries(series.take());
  }

  //
  // Axes
  if (!chart->series().isEmpty()) {
    chart->createDefaultAxes();

    // X-axis
    QValueAxis* xAxis = (QValueAxis*)(chart->axes(Qt::Horizontal).constFirst());
    if (plotParams.xType == PlotArgumentType)
      xAxis->setTitleText("Argument " + QString::number(plotParams.xIdx + 1));
    else {  // template
      if (!custDataName.isEmpty())
        xAxis->setTitleText(custDataName);
      else
        xAxis->setTitleText("Template " + QString::number(plotParams.xIdx + 1));
    }
    xAxis->setTickCount(9);

    // Y-axis
    QValueAxis* yAxis = (QValueAxis*)(chart->axes(Qt::Vertical).constFirst());
    yAxis->setTitleText(getYPlotName(plotParams.yType, bchResults.meta.time_unit));
    yAxis->applyNiceNumbers();
  } else
    chart->setTitle("No series with at least 2 points to display");

  if (init) {
    // View
    mChartView = new QChartView(scopedChart.take(), this);
    mChartView->setRenderHint(QPainter::Antialiasing);
  }
}

void PlotterLineChart::setupOptions(bool init) {
  auto chart = mChartView->chart();

  // General
  if (init) {
    chart->setTheme(QChart::ChartThemeLight);
    chart->legend()->setAlignment(Qt::AlignTop);
    chart->legend()->setShowToolTips(true);
  }
  ui->spinBoxLegendFontSize->setValue(chart->legend()->font().pointSize());

  mIgnoreEvents = true;
  int prevAxisIdx = ui->comboBoxAxis->currentIndex();

  if (!init)  // Re-init
  {
    mAxesParams[0].log = false;
    mAxesParams[1].log = false;
    ui->comboBoxAxis->setCurrentIndex(0);
    ui->checkBoxAxisVisible->setChecked(true);
    ui->checkBoxTitle->setChecked(true);
    ui->checkBoxLog->setChecked(false);
  }

  // Time unit
  if (mCurrentTimeFactor > 1.)
    ui->comboBoxTimeUnit->setCurrentIndex(0);  // ns
  else if (mCurrentTimeFactor < 1.)
    ui->comboBoxTimeUnit->setCurrentIndex(2);  // ms
  else
    ui->comboBoxTimeUnit->setCurrentIndex(1);  // us

  // Axes
  const auto& hAxes = chart->axes(Qt::Horizontal);
  if (!hAxes.isEmpty()) {
    QValueAxis* xAxis = (QValueAxis*)(hAxes.first());
    auto& axisParam = mAxesParams[0];

    axisParam.titleText = xAxis->titleText();
    axisParam.titleSize = xAxis->titleFont().pointSize();
    axisParam.labelFormat = "%g";
    xAxis->setLabelFormat(axisParam.labelFormat);
    axisParam.labelSize = xAxis->labelsFont().pointSize();
    axisParam.min = xAxis->min();
    axisParam.max = xAxis->max();
    axisParam.ticks = xAxis->tickCount();
    axisParam.mticks = xAxis->minorTickCount();

    ui->lineEditTitle->setText(axisParam.titleText);
    ui->lineEditTitle->setCursorPosition(0);
    ui->spinBoxTitleSize->setValue(axisParam.titleSize);
    ui->lineEditFormat->setText(axisParam.labelFormat);
    ui->lineEditFormat->setCursorPosition(0);
    ui->spinBoxLabelSize->setValue(axisParam.labelSize);
    ui->doubleSpinBoxMin->setValue(axisParam.min);
    ui->doubleSpinBoxMax->setValue(axisParam.max);
    ui->spinBoxTicks->setValue(axisParam.ticks);
    ui->spinBoxMTicks->setValue(axisParam.mticks);
  }
  const auto& vAxes = chart->axes(Qt::Vertical);
  if (!vAxes.isEmpty()) {
    QValueAxis* yAxis = (QValueAxis*)(vAxes.first());
    auto& axisParam = mAxesParams[1];

    axisParam.titleText = yAxis->titleText();
    axisParam.titleSize = yAxis->titleFont().pointSize();
    axisParam.labelFormat = "%g";
    yAxis->setLabelFormat(axisParam.labelFormat);
    axisParam.labelSize = yAxis->labelsFont().pointSize();
    axisParam.min = yAxis->min();
    axisParam.max = yAxis->max();
    axisParam.ticks = yAxis->tickCount();
    axisParam.mticks = yAxis->minorTickCount();
  }
  mIgnoreEvents = false;

  // Load options from file
  loadConfig(init);

  // Apply actions
  if (ui->checkBoxAutoReload->isChecked())
    onCheckAutoReload(Qt::Checked);

  // Update series color config
  const auto& chartSeries = chart->series();
  for (int idx = 0; idx < mSeriesMapping.size(); ++idx) {
    auto& config = mSeriesMapping[idx];
    const auto& series = (QXYSeries*)chartSeries.at(idx);

    config.oldColor = series->color();
    if (!config.newColor.isValid())
      config.newColor = series->color();  // init
    else
      series->setColor(config.newColor);  // apply

    if (config.newName != config.oldName)
      series->setName(config.newName.toHtmlEscaped());
  }

  // Restore selected axis
  if (!init)
    ui->comboBoxAxis->setCurrentIndex(prevAxisIdx);

  // Update timestamp
  QDateTime today = QDateTime::currentDateTime();
  QTime now = today.time();
  ui->labelLastReload->setText("(Last: " + now.toString() + ")");
}

void PlotterLineChart::loadConfig(bool init) {
  QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
  settings.beginGroup("lines");

  if (auto value = settings.value("timeUnit"); value.isValid() && !init)
    ui->comboBoxTimeUnit->setCurrentText(value.toString());

  if (auto value = settings.value("autoReload"); value.isValid())
    ui->checkBoxAutoReload->setChecked(value.toBool());

  if (auto value = settings.value("theme"); value.isValid())
    ui->comboBoxTheme->setCurrentText(value.toString());

  if (auto value = settings.value("legend/visible"); value.isValid())
    ui->checkBoxLegendVisible->setChecked(value.toBool());
  if (auto value = settings.value("legend/align"); value.isValid())
    ui->comboBoxLegendAlign->setCurrentText(value.toString());
  if (auto value = settings.value("legend/fontSize", 8); value.isValid())
    ui->spinBoxLegendFontSize->setValue(value.toInt());

  int series_size = settings.beginReadArray("series");
  for (int i = 0; i < series_size; ++i) {
    settings.setArrayIndex(i);
    const auto oldname_value = settings.value("oldName");
    const auto newname_value = settings.value("newName");
    const auto newcolor_value = settings.value("newColor");
    const auto newcolor_valid =
        newcolor_value.isValid() && QColor::isValidColor(newcolor_value.toString());

    if (oldname_value.isValid() && newname_value.isValid() && newcolor_valid) {
      SeriesConfig saved_config(oldname_value.toString(), "");

      if (int idx = mSeriesMapping.indexOf(saved_config); idx >= 0) {
        mSeriesMapping[idx].newName = newname_value.toString();
        mSeriesMapping[idx].newColor.setNamedColor(newcolor_value.toString());
      }
    }
  }
  settings.endArray();

  const QStringList prefixes{"axis/x", "axis/y"};
  const QList<int> ticks{9, 5};

  for (int i = 0; i < mAxesParams.size(); ++i) {
    auto& axis = mAxesParams[i];
    const auto prefix = prefixes[i];
    ui->comboBoxAxis->setCurrentIndex(i);

    if (auto value = settings.value(prefix + "/visible"); value.isValid()) {
      axis.visible = value.toBool();
      ui->checkBoxAxisVisible->setChecked(axis.visible);
    }
    if (auto value = settings.value(prefix + "/title"); value.isValid()) {
      axis.title = value.toBool();
      ui->checkBoxTitle->setChecked(axis.title);
    }
    if (auto value = settings.value(prefix + "/log"); value.isValid()) {
      axis.log = value.toBool();
      ui->checkBoxLog->setChecked(axis.log);
    }
    if (auto value = settings.value(prefix + "/logBase", 10); value.isValid()) {
      axis.logBase = value.toInt();
      ui->spinBoxLogBase->setValue(axis.logBase);
    }
    if (auto value = settings.value(prefix + "/titleSize", 8); value.isValid()) {
      axis.titleSize = value.toInt();
      ui->spinBoxTitleSize->setValue(axis.titleSize);
    }
    if (auto value = settings.value(prefix + "/labelFormat"); value.isValid()) {
      axis.labelFormat = value.toString();
      ui->lineEditFormat->setText(axis.labelFormat);
      ui->lineEditFormat->setCursorPosition(0);
    }
    if (auto value = settings.value(prefix + "/labelSize", 8); value.isValid()) {
      axis.labelSize = value.toInt();
      ui->spinBoxLabelSize->setValue(axis.labelSize);
    }
    if (auto value = settings.value(prefix + "/ticks", ticks[i]); value.isValid()) {
      axis.ticks = value.toInt();
      ui->spinBoxTicks->setValue(axis.ticks);
    }
    if (auto value = settings.value(prefix + "/mticks", 0); value.isValid()) {
      axis.mticks = value.toInt();
      ui->spinBoxMTicks->setValue(axis.mticks);
    }
    if (auto value = settings.value(prefix + "/titleText"); value.isValid() && !init) {
      axis.titleText = value.toString();
      ui->lineEditTitle->setText(axis.titleText);
      ui->lineEditTitle->setCursorPosition(0);
    }
    if (auto value = settings.value(prefix + "/min"); value.isValid() && i == 1) {
      axis.min = value.toDouble();
      ui->doubleSpinBoxMin->setValue(axis.min);
    }
    if (auto value = settings.value(prefix + "/max"); value.isValid() && i == 1) {
      axis.max = value.toDouble();
      ui->doubleSpinBoxMax->setValue(axis.max);
    }
  }
  ui->comboBoxAxis->setCurrentIndex(0);
  settings.endGroup();
}

void PlotterLineChart::saveConfig() {
  QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
  settings.beginGroup("lines");

  settings.setValue("autoReload", ui->checkBoxAutoReload->isChecked());
  settings.setValue("timeUnit", ui->comboBoxTimeUnit->currentText());
  settings.setValue("theme", ui->comboBoxTheme->currentText());

  settings.setValue("legend/visible", ui->checkBoxLegendVisible->isChecked());
  settings.setValue("legend/align", ui->comboBoxLegendAlign->currentText());
  settings.setValue("legend/fontSize", ui->spinBoxLegendFontSize->value());

  settings.beginWriteArray("series");
  for (int i = 0; i < mSeriesMapping.size(); ++i) {
    settings.setArrayIndex(i);
    settings.setValue("oldName", mSeriesMapping.at(i).oldName);
    settings.setValue("newName", mSeriesMapping.at(i).newName);
    settings.setValue("newColor", mSeriesMapping.at(i).newColor.name());
  }
  settings.endArray();

  QStringList prefixes{"axis/x", "axis/y"};
  for (int i = 0; i < mAxesParams.size(); ++i) {
    const auto& axis = mAxesParams[i];
    const auto prefix = prefixes[i];
    settings.setValue(prefix + "/visible", axis.visible);
    settings.setValue(prefix + "/title", axis.title);
    settings.setValue(prefix + "/log", axis.log);
    settings.setValue(prefix + "/logBase", axis.logBase);
    settings.setValue(prefix + "/titleText", axis.titleText);
    settings.setValue(prefix + "/titleSize", axis.titleSize);
    settings.setValue(prefix + "/labelFormat", axis.labelFormat);
    settings.setValue(prefix + "/labelSize", axis.labelSize);
    settings.setValue(prefix + "/min", axis.min);
    settings.setValue(prefix + "/max", axis.max);
    settings.setValue(prefix + "/ticks", axis.ticks);
    settings.setValue(prefix + "/mticks", axis.mticks);
  }

  settings.endGroup();
}

//
// Theme
void PlotterLineChart::onComboThemeChanged(int index) {
  QChart::ChartTheme theme =
      static_cast<QChart::ChartTheme>(ui->comboBoxTheme->itemData(index).toInt());
  mChartView->chart()->setTheme(theme);

  // Update series color
  const auto& chartSeries = mChartView->chart()->series();
  for (int idx = 0; idx < mSeriesMapping.size(); ++idx) {
    auto& config = mSeriesMapping[idx];
    const auto& series = (QXYSeries*)chartSeries.at(idx);
    auto prevColor = config.oldColor;

    config.oldColor = series->color();
    if (config.newColor != prevColor)
      series->setColor(config.newColor);  // re-apply config
    else
      config.newColor = config.oldColor;  // sync with theme
  }

  // Re-apply font sizes
  onSpinLegendFontSizeChanged(ui->spinBoxLegendFontSize->value());
  onSpinLabelSizeChanged2(mAxesParams[0].labelSize, 0);
  onSpinLabelSizeChanged2(mAxesParams[1].labelSize, 1);
  onSpinTitleSizeChanged2(mAxesParams[0].titleSize, 0);
  onSpinTitleSizeChanged2(mAxesParams[1].titleSize, 1);
}

//
// Legend
void PlotterLineChart::onCheckLegendVisible(int state) {
  mChartView->chart()->legend()->setVisible(state == Qt::Checked);
}

void PlotterLineChart::onComboLegendAlignChanged(int index) {
  Qt::Alignment align =
      static_cast<Qt::Alignment>(ui->comboBoxLegendAlign->itemData(index).toInt());
  mChartView->chart()->legend()->setAlignment(align);
}

void PlotterLineChart::onSpinLegendFontSizeChanged(int i) {
  QFont font = mChartView->chart()->legend()->font();
  font.setPointSize(i);
  mChartView->chart()->legend()->setFont(font);
}

void PlotterLineChart::onSeriesEditClicked() {
  SeriesDialog seriesDialog(mSeriesMapping, this);
  auto res = seriesDialog.exec();
  if (res == QDialog::Accepted) {
    const auto& newMapping = seriesDialog.getMapping();
    for (int idx = 0; idx < newMapping.size(); ++idx) {
      const auto& newPair = newMapping[idx];
      const auto& oldPair = mSeriesMapping[idx];
      auto series = (QXYSeries*)mChartView->chart()->series().at(idx);
      if (newPair.newName != oldPair.newName) {
        series->setName(newPair.newName.toHtmlEscaped());
      }
      if (newPair.newColor != oldPair.newColor) {
        series->setColor(newPair.newColor);
      }
    }
    mSeriesMapping = newMapping;
  }
}

void PlotterLineChart::onComboTimeUnitChanged(int /*index*/) {
  if (mIgnoreEvents)
    return;

  // Update data
  double unitFactor = ui->comboBoxTimeUnit->currentData().toDouble();
  double updateFactor = unitFactor / mCurrentTimeFactor;  // can cause precision loss
  auto chartSeries = mChartView->chart()->series();
  for (auto& series : chartSeries) {
    auto xySeries = (QXYSeries*)series;
    auto points = xySeries->pointsVector();
    for (auto& point : points) {
      point.setY(point.y() * updateFactor);
    }
    xySeries->replace(points);
  }

  // Update axis title
  QString oldUnitName = "(us)";
  if (mCurrentTimeFactor > 1.)
    oldUnitName = "(ns)";
  else if (mCurrentTimeFactor < 1.)
    oldUnitName = "(ms)";

  const auto& axes = mChartView->chart()->axes(Qt::Vertical);
  if (!axes.isEmpty()) {
    QAbstractAxis* axis = axes.first();
    QString axisTitle = axis->titleText();
    if (axisTitle.endsWith(oldUnitName)) {
      QString unitName = ui->comboBoxTimeUnit->currentText();
      onEditTitleChanged2(axisTitle.replace(axisTitle.size() - 3, 2, unitName), 1);
    }
  }
  // Update range
  if (ui->comboBoxAxis->currentIndex() == 1) {
    ui->doubleSpinBoxMin->setValue(mAxesParams[1].min * updateFactor);
    ui->doubleSpinBoxMax->setValue(mAxesParams[1].max * updateFactor);
  } else {
    onSpinMinChanged2(mAxesParams[1].min * updateFactor, 1);
    onSpinMaxChanged2(mAxesParams[1].max * updateFactor, 1);
  }

  mCurrentTimeFactor = unitFactor;
}

//
// Axes
void PlotterLineChart::onComboAxisChanged(int idx) {
  // Update UI
  bool wasIgnoring = mIgnoreEvents;
  mIgnoreEvents = true;

  ui->checkBoxAxisVisible->setChecked(mAxesParams[idx].visible);
  ui->checkBoxTitle->setChecked(mAxesParams[idx].title);
  ui->checkBoxLog->setChecked(mAxesParams[idx].log);
  ui->spinBoxLogBase->setValue(mAxesParams[idx].logBase);
  ui->lineEditTitle->setText(mAxesParams[idx].titleText);
  ui->lineEditTitle->setCursorPosition(0);
  ui->spinBoxTitleSize->setValue(mAxesParams[idx].titleSize);
  ui->lineEditFormat->setText(mAxesParams[idx].labelFormat);
  ui->lineEditFormat->setCursorPosition(0);
  ui->spinBoxLabelSize->setValue(mAxesParams[idx].labelSize);
  ui->doubleSpinBoxMin->setDecimals(idx == 1 ? 6 : 3);
  ui->doubleSpinBoxMax->setDecimals(idx == 1 ? 6 : 3);
  ui->doubleSpinBoxMin->setValue(mAxesParams[idx].min);
  ui->doubleSpinBoxMax->setValue(mAxesParams[idx].max);
  ui->doubleSpinBoxMin->setSingleStep(idx == 1 ? 0.1 : 1.0);
  ui->doubleSpinBoxMax->setSingleStep(idx == 1 ? 0.1 : 1.0);
  ui->spinBoxTicks->setValue(mAxesParams[idx].ticks);
  ui->spinBoxMTicks->setValue(mAxesParams[idx].mticks);

  ui->spinBoxTicks->setEnabled(!mAxesParams[idx].log);
  ui->spinBoxLogBase->setEnabled(mAxesParams[idx].log);

  mIgnoreEvents = wasIgnoring;
}

void PlotterLineChart::onCheckAxisVisible(int state) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty()) {
    QAbstractAxis* axis = axes.first();
    axis->setVisible(state == Qt::Checked);
    mAxesParams[iAxis].visible = state == Qt::Checked;
  }
}

void PlotterLineChart::onCheckTitleVisible(int state) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty()) {
    QAbstractAxis* axis = axes.first();
    axis->setTitleVisible(state == Qt::Checked);
    mAxesParams[iAxis].title = state == Qt::Checked;
  }
}

void PlotterLineChart::onCheckLog(int state) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;
  Qt::Alignment align = iAxis == 0 ? Qt::AlignBottom : Qt::AlignLeft;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty()) {
    if (state == Qt::Checked) {
      QValueAxis* axis = (QValueAxis*)(axes.first());

      QLogValueAxis* logAxis = new QLogValueAxis();
      logAxis->setVisible(axis->isVisible());
      logAxis->setTitleVisible(axis->isTitleVisible());
      logAxis->setTitleText(axis->titleText());
      logAxis->setTitleFont(axis->titleFont());
      logAxis->setLabelFormat(axis->labelFormat());
      logAxis->setLabelsFont(axis->labelsFont());

      mChartView->chart()->removeAxis(axis);
      mChartView->chart()->addAxis(logAxis, align);
      const auto chartSeries = mChartView->chart()->series();
      for (const auto& series : chartSeries)
        series->attachAxis(logAxis);

      logAxis->setBase(mAxesParams[iAxis].logBase);
      logAxis->setMin(mAxesParams[iAxis].min);
      logAxis->setMax(mAxesParams[iAxis].max);
      logAxis->setMinorTickCount(mAxesParams[iAxis].mticks);
    } else {
      QLogValueAxis* logAxis = (QLogValueAxis*)(axes.first());

      QValueAxis* axis = new QValueAxis();
      axis->setVisible(logAxis->isVisible());
      axis->setTitleVisible(logAxis->isTitleVisible());
      axis->setTitleText(logAxis->titleText());
      axis->setTitleFont(logAxis->titleFont());
      axis->setLabelFormat(logAxis->labelFormat());
      axis->setLabelsFont(logAxis->labelsFont());

      mChartView->chart()->removeAxis(logAxis);
      mChartView->chart()->addAxis(axis, align);
      const auto chartSeries = mChartView->chart()->series();
      for (const auto& series : chartSeries)
        series->attachAxis(axis);

      axis->setMin(mAxesParams[iAxis].min);
      axis->setMax(mAxesParams[iAxis].max);
      axis->setTickCount(mAxesParams[iAxis].ticks);
      axis->setMinorTickCount(mAxesParams[iAxis].mticks);
    }
    ui->spinBoxTicks->setEnabled(state != Qt::Checked);
    ui->spinBoxLogBase->setEnabled(state == Qt::Checked);
    mAxesParams[iAxis].log = state == Qt::Checked;
  }
}

void PlotterLineChart::onSpinLogBaseChanged(int i) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty() && ui->checkBoxLog->isChecked()) {
    QLogValueAxis* logAxis = (QLogValueAxis*)(axes.first());
    logAxis->setBase(i);
    mAxesParams[iAxis].logBase = i;
  }
}

void PlotterLineChart::onEditTitleChanged(const QString& text) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();

  onEditTitleChanged2(text, iAxis);
}

void PlotterLineChart::onEditTitleChanged2(const QString& text, int iAxis) {
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty()) {
    QAbstractAxis* axis = axes.first();
    axis->setTitleText(text);
    mAxesParams[iAxis].titleText = text;
  }
}

void PlotterLineChart::onSpinTitleSizeChanged(int i) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();

  onSpinTitleSizeChanged2(i, iAxis);
}

void PlotterLineChart::onSpinTitleSizeChanged2(int i, int iAxis) {
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty()) {
    QAbstractAxis* axis = axes.first();

    QFont font = axis->titleFont();
    font.setPointSize(i);
    axis->setTitleFont(font);
    mAxesParams[iAxis].titleSize = i;
  }
}

void PlotterLineChart::onEditFormatChanged(const QString& text) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty()) {
    if (!ui->checkBoxLog->isChecked()) {
      QValueAxis* axis = (QValueAxis*)(axes.first());
      axis->setLabelFormat(text);
    } else {
      QLogValueAxis* axis = (QLogValueAxis*)(axes.first());
      axis->setLabelFormat(text);
    }
    mAxesParams[iAxis].labelFormat = text;
  }
}

void PlotterLineChart::onSpinLabelSizeChanged(int i) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();

  onSpinLabelSizeChanged2(i, iAxis);
}

void PlotterLineChart::onSpinLabelSizeChanged2(int i, int iAxis) {
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty()) {
    QAbstractAxis* axis = axes.first();

    QFont font = axis->labelsFont();
    font.setPointSize(i);
    axis->setLabelsFont(font);
    mAxesParams[iAxis].labelSize = i;
  }
}

void PlotterLineChart::onSpinMinChanged(double d) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();

  onSpinMinChanged2(d, iAxis);
}

void PlotterLineChart::onSpinMinChanged2(double d, int iAxis) {
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty()) {
    QAbstractAxis* axis = axes.first();
    axis->setMin(d);
    mAxesParams[iAxis].min = d;
  }
}

void PlotterLineChart::onSpinMaxChanged(double d) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();

  onSpinMaxChanged2(d, iAxis);
}

void PlotterLineChart::onSpinMaxChanged2(double d, int iAxis) {
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty()) {
    QAbstractAxis* axis = axes.first();
    axis->setMax(d);
    mAxesParams[iAxis].max = d;
  }
}

void PlotterLineChart::onSpinTicksChanged(int i) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty()) {
    if (!ui->checkBoxLog->isChecked()) {
      QValueAxis* axis = (QValueAxis*)(axes.first());
      axis->setTickCount(i);
      mAxesParams[iAxis].ticks = i;
    }
  }
}

void PlotterLineChart::onSpinMTicksChanged(int i) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty()) {
    if (!ui->checkBoxLog->isChecked()) {
      QValueAxis* axis = (QValueAxis*)(axes.first());
      axis->setMinorTickCount(i);
    } else {
      QLogValueAxis* axis = (QLogValueAxis*)(axes.first());
      axis->setMinorTickCount(i);

      // Force update
      const int base = (int)axis->base();
      axis->setBase(base + 1);
      axis->setBase(base);
    }
    mAxesParams[iAxis].mticks = i;
  }
}

//
// Actions
void PlotterLineChart::onCheckAutoReload(int state) {
  if (state == Qt::Checked) {
    if (mWatcher.files().empty()) {
      mWatcher.addPath(mOrigFilename);
      for (const auto& addFilename : qAsConst(mAddFilenames))
        mWatcher.addPath(addFilename.filename);
    }
  } else {
    if (!mWatcher.files().empty())
      mWatcher.removePaths(mWatcher.files());
  }
}

void PlotterLineChart::onAutoReload(const QString& path) {
  QFileInfo fi(path);
  if (fi.exists() && fi.isReadable() && fi.size() > 0)
    onReloadClicked();
  else
    qWarning() << "Unable to auto-reload file: " << path;
}

void PlotterLineChart::onReloadClicked() {
  // Load new results
  QString errorMsg;
  BenchResults newBchResults = ResultParser::parseJsonFile(mOrigFilename, errorMsg);

  if (newBchResults.benchmarks.isEmpty()) {
    QMessageBox::critical(this, "Chart reload",
                          "Error parsing original file: " + mOrigFilename + " -> " + errorMsg);
    return;
  }
  for (const auto& addFile : qAsConst(mAddFilenames)) {
    errorMsg.clear();
    BenchResults newAddResults = ResultParser::parseJsonFile(addFile.filename, errorMsg);
    if (newAddResults.benchmarks.isEmpty()) {
      QMessageBox::critical(
          this, "Chart reload",
          "Error parsing additional file: " + addFile.filename + " -> " + errorMsg);
      return;
    }
    if (addFile.isAppend)
      newBchResults.appendResults(newAddResults);
    else
      newBchResults.overwriteResults(newAddResults);
  }

  // Check compatibility with previous
  errorMsg.clear();
  if (mBenchIdxs.size() != newBchResults.benchmarks.size()) {
    errorMsg = "Number of series/points is different";
    if (mAllIndexes) {
      mBenchIdxs.clear();
      for (int i = 0; i < newBchResults.benchmarks.size(); ++i)
        mBenchIdxs.append(i);
    }
  }

  QVector<BenchSubset> newBchSubsets = newBchResults.groupParam(
      mPlotParams.xType == PlotArgumentType, mBenchIdxs, mPlotParams.xIdx, "X");
  const auto& oldChartSeries = mChartView->chart()->series();
  int newSeriesIdx = 0;
  if (errorMsg.isEmpty()) {
    for (const auto& bchSubset : qAsConst(newBchSubsets)) {
      // Ignore single point lines
      if (bchSubset.idxs.size() < 2)
        continue;
      if (newSeriesIdx >= oldChartSeries.size())
        break;

      const QString& subsetName = bchSubset.name;
      if (subsetName != mSeriesMapping[newSeriesIdx].oldName) {
        errorMsg = "Series has different name";
        break;
      }
      const auto lineSeries = (QLineSeries*)(oldChartSeries[newSeriesIdx]);
      if (bchSubset.idxs.size() != lineSeries->count()) {
        errorMsg = "Series has different number of points";
        break;
      }
      ++newSeriesIdx;
    }
    if (newSeriesIdx != oldChartSeries.size()) {
      errorMsg = "Number of series is different";
    }
  }

  // Direct update if compatible
  if (errorMsg.isEmpty()) {
    bool custDataAxis = true;
    QString custDataName;
    newSeriesIdx = 0;
    for (const auto& bchSubset : qAsConst(newBchSubsets)) {
      // Ignore single point lines
      if (bchSubset.idxs.size() < 2) {
        qWarning() << "Not enough points to trace line for: " << bchSubset.name;
        continue;
      }

      // Update points
      QXYSeries* oldSeries = (QXYSeries*)oldChartSeries[newSeriesIdx];
      oldSeries->clear();

      double xFallback = 0.;
      for (int idx : bchSubset.idxs) {
        QString xName = newBchResults.getParamName(mPlotParams.xType == PlotArgumentType, idx,
                                                   mPlotParams.xIdx);
        double xVal = BenchResults::getParamValue(xName, custDataName, custDataAxis, xFallback);

        // Add point
        oldSeries->append(xVal, getYPlotValue(newBchResults.benchmarks[idx], mPlotParams.yType) *
                                    mCurrentTimeFactor);
      }
      ++newSeriesIdx;
    }
  }
  // Reset update if all benchmarks
  else if (mAllIndexes) {
    saveConfig();
    setupChart(newBchResults, mBenchIdxs, mPlotParams, false);
    setupOptions(false);
  } else {
    QMessageBox::critical(this, "Chart reload", errorMsg);
    return;
  }

  // Update timestamp
  QDateTime today = QDateTime::currentDateTime();
  QTime now = today.time();
  ui->labelLastReload->setText("(Last: " + now.toString() + ")");
}

void PlotterLineChart::onSnapshotClicked() {
  QString fileName =
      QFileDialog::getSaveFileName(this, tr("Save snapshot"), "", tr("Images (*.png)"));

  if (!fileName.isEmpty()) {
    QPixmap pixmap = mChartView->grab();

    bool ok = pixmap.save(fileName, "PNG");
    if (!ok)
      QMessageBox::warning(this, "Chart snapshot", "Error saving snapshot file.");
  }
}
