/* Copyright 2019 Guillaume AUJAY. All rights reserved.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>. */

#include "plotter_boxchart.h"

#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QtCharts>

#include "benchmark_results.h"
#include "result_parser.h"
#include "ui_plotter_boxchart.h"

namespace {
const bool kForceConfig = false;
}

PlotterBoxChart::PlotterBoxChart(BenchResults& bchResults, const QVector<int>& bchIdxs,
                                 const PlotParams& plotParams, const QString& origFilename,
                                 const QVector<FileReload>& addFilenames, QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::PlotterBoxChart)
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
  this->setWindowTitle("Boxes - " + fileInfo.fileName());

  connectUI();

  // Init
  setupChart(bchResults, bchIdxs, plotParams);
  setupOptions();

  // Show
  ui->horizontalLayout->insertWidget(0, mChartView);
}

PlotterBoxChart::~PlotterBoxChart() {
  // Save options to file
  saveConfig();

  delete ui;
}

void PlotterBoxChart::connectUI() {
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
          &PlotterBoxChart::onComboThemeChanged);

  // Legend
  connect(ui->checkBoxLegendVisible, &QCheckBox::stateChanged, this,
          &PlotterBoxChart::onCheckLegendVisible);

  ui->comboBoxLegendAlign->addItem("Top", Qt::AlignTop);
  ui->comboBoxLegendAlign->addItem("Bottom", Qt::AlignBottom);
  ui->comboBoxLegendAlign->addItem("Left", Qt::AlignLeft);
  ui->comboBoxLegendAlign->addItem("Right", Qt::AlignRight);
  connect(ui->comboBoxLegendAlign, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &PlotterBoxChart::onComboLegendAlignChanged);

  connect(ui->spinBoxLegendFontSize, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &PlotterBoxChart::onSpinLegendFontSizeChanged);
  connect(ui->pushButtonSeries, &QPushButton::clicked, this, &PlotterBoxChart::onSeriesEditClicked);

  if (!isYTimeBased(mPlotParams.yType))
    ui->comboBoxTimeUnit->setEnabled(false);
  else {
    ui->comboBoxTimeUnit->addItem("ns", 1000.);
    ui->comboBoxTimeUnit->addItem("us", 1.);
    ui->comboBoxTimeUnit->addItem("ms", 0.001);
    connect(ui->comboBoxTimeUnit, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PlotterBoxChart::onComboTimeUnitChanged);
  }

  // Axes
  ui->comboBoxAxis->addItem("X-Axis");
  ui->comboBoxAxis->addItem("Y-Axis");
  connect(ui->comboBoxAxis, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &PlotterBoxChart::onComboAxisChanged);

  connect(ui->checkBoxAxisVisible, &QCheckBox::stateChanged, this,
          &PlotterBoxChart::onCheckAxisVisible);
  connect(ui->checkBoxTitle, &QCheckBox::stateChanged, this, &PlotterBoxChart::onCheckTitleVisible);
  connect(ui->checkBoxLog, &QCheckBox::stateChanged, this, &PlotterBoxChart::onCheckLog);
  connect(ui->spinBoxLogBase, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &PlotterBoxChart::onSpinLogBaseChanged);
  connect(ui->lineEditTitle, &QLineEdit::textChanged, this, &PlotterBoxChart::onEditTitleChanged);
  connect(ui->spinBoxTitleSize, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &PlotterBoxChart::onSpinTitleSizeChanged);
  connect(ui->lineEditFormat, &QLineEdit::textChanged, this, &PlotterBoxChart::onEditFormatChanged);
  connect(ui->spinBoxLabelSize, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &PlotterBoxChart::onSpinLabelSizeChanged);
  connect(ui->doubleSpinBoxMin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          &PlotterBoxChart::onSpinMinChanged);
  connect(ui->doubleSpinBoxMax, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          &PlotterBoxChart::onSpinMaxChanged);
  connect(ui->comboBoxMin, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &PlotterBoxChart::onComboMinChanged);
  connect(ui->comboBoxMax, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &PlotterBoxChart::onComboMaxChanged);
  connect(ui->spinBoxTicks, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &PlotterBoxChart::onSpinTicksChanged);
  connect(ui->spinBoxMTicks, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &PlotterBoxChart::onSpinMTicksChanged);

  // Actions
  connect(&mWatcher, &QFileSystemWatcher::fileChanged, this, &PlotterBoxChart::onAutoReload);
  connect(ui->checkBoxAutoReload, &QCheckBox::stateChanged, this,
          &PlotterBoxChart::onCheckAutoReload);
  connect(ui->pushButtonReload, &QPushButton::clicked, this, &PlotterBoxChart::onReloadClicked);
  connect(ui->pushButtonSnapshot, &QPushButton::clicked, this, &PlotterBoxChart::onSnapshotClicked);
}

void PlotterBoxChart::setupChart(BenchResults& bchResults, const QVector<int>& bchIdxs,
                                 const PlotParams& plotParams, bool init) {
  //    QScopedPointer<QChart> scopedChart(new QChart());
  //    QChart* chart = scopedChart.get();
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

  // 2D Boxes and whiskers
  // X: argumentA or templateB
  // Y: time/iter/bytes/items (not name dependent)
  // Box: one per benchmark % X-param
  QVector<BenchSubset> bchSubsets =
      bchResults.groupParam(plotParams.xType == PlotArgumentType, bchIdxs, plotParams.xIdx, "X");
  for (const auto& bchSubset : qAsConst(bchSubsets)) {
    // Series = benchmark % X-param
    QScopedPointer<QBoxPlotSeries> series(new QBoxPlotSeries());

    const QString& subsetName = bchSubset.name;
    //        qDebug() << "subsetName:" << subsetName;
    //        qDebug() << "subsetIdxs:" << bchSubset.idxs;

    for (int idx : bchSubset.idxs) {
      QString xName =
          bchResults.getParamName(plotParams.xType == PlotArgumentType, idx, plotParams.xIdx);
      BenchYStats yStats = getYPlotStats(bchResults.benchmarks[idx], plotParams.yType);

      // BoxSet
      QScopedPointer<QBoxSet> box(new QBoxSet(xName.toHtmlEscaped()));
      box->setValue(QBoxSet::LowerExtreme, yStats.min * mCurrentTimeFactor);
      box->setValue(QBoxSet::UpperExtreme, yStats.max * mCurrentTimeFactor);
      box->setValue(QBoxSet::Median, yStats.median * mCurrentTimeFactor);
      box->setValue(QBoxSet::LowerQuartile, yStats.lowQuart * mCurrentTimeFactor);
      box->setValue(QBoxSet::UpperQuartile, yStats.uppQuart * mCurrentTimeFactor);

      series->append(box.take());
    }
    // Add series
    series->setName(subsetName.toHtmlEscaped());
    mSeriesMapping.push_back({subsetName, subsetName});  // color set later
    chart->addSeries(series.take());
  }

  // Axes
  if (!chart->series().isEmpty()) {
    chart->legend()->setVisible(true);
    chart->createDefaultAxes();

    // X-axis
    QBarCategoryAxis* xAxis = (QBarCategoryAxis*)(chart->axes(Qt::Horizontal).constFirst());
    if (plotParams.xType == PlotArgumentType)
      xAxis->setTitleText("Argument " + QString::number(plotParams.xIdx + 1));
    else if (plotParams.xType == PlotTemplateType)
      xAxis->setTitleText("Template " + QString::number(plotParams.xIdx + 1));
    if (plotParams.xType != PlotEmptyType)
      xAxis->setTitleVisible(true);

    // Y-axis
    QValueAxis* yAxis = (QValueAxis*)(chart->axes(Qt::Vertical).constFirst());
    yAxis->setTitleText(getYPlotName(plotParams.yType, bchResults.meta.time_unit));
    yAxis->applyNiceNumbers();
  } else
    chart->setTitle("No compatible series to display");

  if (init) {
    // View
    mChartView = new QChartView(scopedChart.take(), this);
    mChartView->setRenderHint(QPainter::Antialiasing);
  }
}

void PlotterBoxChart::setupOptions(bool init) {
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
    mAxesParams[1].visible = true;
    mAxesParams[1].title = true;
    ui->comboBoxAxis->setCurrentIndex(0);
    ui->comboBoxMin->clear();
    ui->comboBoxMax->clear();
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
  const auto& xAxes = chart->axes(Qt::Horizontal);
  if (!xAxes.isEmpty()) {
    QBarCategoryAxis* xAxis = (QBarCategoryAxis*)(xAxes.first());
    auto& axisParam = mAxesParams[0];

    axisParam.titleText = xAxis->titleText();
    axisParam.titleSize = xAxis->titleFont().pointSize();
    axisParam.labelSize = xAxis->labelsFont().pointSize();

    ui->doubleSpinBoxMin->setVisible(false);
    ui->doubleSpinBoxMax->setVisible(false);

    ui->lineEditTitle->setText(axisParam.titleText);
    ui->lineEditTitle->setCursorPosition(0);
    ui->spinBoxTitleSize->setValue(axisParam.titleSize);
    ui->spinBoxLabelSize->setValue(axisParam.labelSize);
    const auto categories = xAxis->categories();
    for (const auto& cat : categories) {
      ui->comboBoxMin->addItem(cat);
      ui->comboBoxMax->addItem(cat);
    }
    ui->comboBoxMax->setCurrentIndex(ui->comboBoxMax->count() - 1);
  }
  const auto& yAxes = chart->axes(Qt::Vertical);
  if (!yAxes.isEmpty()) {
    QValueAxis* yAxis = (QValueAxis*)(yAxes.first());
    auto& axisParam = mAxesParams[1];

    axisParam.titleText = yAxis->titleText();
    axisParam.titleSize = yAxis->titleFont().pointSize();
    axisParam.labelSize = yAxis->labelsFont().pointSize();

    ui->lineEditFormat->setText("%g");
    ui->lineEditFormat->setCursorPosition(0);
    yAxis->setLabelFormat(ui->lineEditFormat->text());
    ui->doubleSpinBoxMin->setValue(yAxis->min());
    ui->doubleSpinBoxMax->setValue(yAxis->max());
    ui->spinBoxTicks->setValue(yAxis->tickCount());
    ui->spinBoxMTicks->setValue(yAxis->minorTickCount());
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
    const auto& series = (QBoxPlotSeries*)chartSeries.at(idx);

    config.oldColor = series->brush().color();
    if (!config.newColor.isValid())
      config.newColor = series->brush().color();  // init
    else {
      auto brush = series->brush();
      brush.setColor(config.newColor);  // apply
      series->setBrush(brush);
    }

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

void PlotterBoxChart::loadConfig(bool init) {
  QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
  settings.beginGroup("boxes");

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

  QStringList prefixes{"axis/x", "axis/y"};
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
    if (auto value = settings.value(prefix + "/titleSize", 8); value.isValid()) {
      axis.titleSize = value.toInt();
      ui->spinBoxTitleSize->setValue(axis.titleSize);
    }
    if (auto value = settings.value(prefix + "/labelSize", 8); value.isValid()) {
      axis.labelSize = value.toInt();
      ui->spinBoxLabelSize->setValue(axis.labelSize);
    }
    if (auto value = settings.value(prefix + "/titleText"); value.isValid() && !init) {
      axis.titleText = value.toString();
      ui->lineEditTitle->setText(axis.titleText);
      ui->lineEditTitle->setCursorPosition(0);
    }

    if (i == 0)  // x-axis
    {
      if (auto value = settings.value(prefix + "/min"); value.isValid() && kForceConfig)
        ui->comboBoxMin->setCurrentText(value.toString());
      if (auto value = settings.value(prefix + "/max"); value.isValid() && kForceConfig)
        ui->comboBoxMax->setCurrentText(value.toString());
    } else  // y-axis
    {
      if (auto value = settings.value(prefix + "/log"); value.isValid())
        ui->checkBoxLog->setChecked(value.toBool());
      if (auto value = settings.value(prefix + "/logBase", 10); value.isValid())
        ui->spinBoxLogBase->setValue(value.toInt());
      if (auto value = settings.value(prefix + "/labelFormat"); value.isValid()) {
        ui->lineEditFormat->setText(value.toString());
        ui->lineEditFormat->setCursorPosition(0);
      }
      if (auto value = settings.value(prefix + "/ticks", 5); value.isValid())
        ui->spinBoxTicks->setValue(value.toInt());
      if (auto value = settings.value(prefix + "/mticks", 0); value.isValid())
        ui->spinBoxMTicks->setValue(value.toInt());
      if (auto value = settings.value(prefix + "/min"); value.isValid() && !init)
        ui->doubleSpinBoxMin->setValue(value.toDouble());
      if (auto value = settings.value(prefix + "/max"); value.isValid() && !init)
        ui->doubleSpinBoxMax->setValue(value.toDouble());
    }
  }
  ui->comboBoxAxis->setCurrentIndex(0);
  settings.endGroup();
}

void PlotterBoxChart::saveConfig() {
  QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
  settings.beginGroup("boxes");

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
    settings.setValue(prefix + "/titleText", axis.titleText);
    settings.setValue(prefix + "/titleSize", axis.titleSize);
    settings.setValue(prefix + "/labelSize", axis.labelSize);

    if (i == 0)  // x-axis
    {
      settings.setValue(prefix + "/min", ui->comboBoxMin->currentText());
      settings.setValue(prefix + "/max", ui->comboBoxMax->currentText());
    } else  // y-axis
    {
      settings.setValue(prefix + "/log", ui->checkBoxLog->isChecked());
      settings.setValue(prefix + "/logBase", ui->spinBoxLogBase->value());
      settings.setValue(prefix + "/labelFormat", ui->lineEditFormat->text());
      settings.setValue(prefix + "/min", ui->doubleSpinBoxMin->value());
      settings.setValue(prefix + "/max", ui->doubleSpinBoxMax->value());
      settings.setValue(prefix + "/ticks", ui->spinBoxTicks->value());
      settings.setValue(prefix + "/mticks", ui->spinBoxMTicks->value());
    }
  }
  settings.endGroup();
}

//
// Theme
void PlotterBoxChart::onComboThemeChanged(int index) {
  QChart::ChartTheme theme =
      static_cast<QChart::ChartTheme>(ui->comboBoxTheme->itemData(index).toInt());
  mChartView->chart()->setTheme(theme);

  // Update series color
  const auto& chartSeries = mChartView->chart()->series();
  for (int idx = 0; idx < mSeriesMapping.size(); ++idx) {
    auto& config = mSeriesMapping[idx];
    auto series = (QBoxPlotSeries*)chartSeries.at(idx);
    auto prevColor = config.oldColor;

    auto brush = series->brush();
    config.oldColor = brush.color();
    if (config.newColor != prevColor) {
      brush.setColor(config.newColor);
      series->setBrush(brush);  // re-apply config
    } else
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
void PlotterBoxChart::onCheckLegendVisible(int state) {
  mChartView->chart()->legend()->setVisible(state == Qt::Checked);
}

void PlotterBoxChart::onComboLegendAlignChanged(int index) {
  Qt::Alignment align =
      static_cast<Qt::Alignment>(ui->comboBoxLegendAlign->itemData(index).toInt());
  mChartView->chart()->legend()->setAlignment(align);
}

void PlotterBoxChart::onSpinLegendFontSizeChanged(int i) {
  QFont font = mChartView->chart()->legend()->font();
  font.setPointSize(i);
  mChartView->chart()->legend()->setFont(font);
}

void PlotterBoxChart::onSeriesEditClicked() {
  SeriesDialog seriesDialog(mSeriesMapping, this);
  auto res = seriesDialog.exec();
  if (res == QDialog::Accepted) {
    const auto& newMapping = seriesDialog.getMapping();
    for (int idx = 0; idx < newMapping.size(); ++idx) {
      const auto& newPair = newMapping[idx];
      const auto& oldPair = mSeriesMapping[idx];
      auto series = (QBoxPlotSeries*)mChartView->chart()->series().at(idx);
      if (newPair.newName != oldPair.newName) {
        series->setName(newPair.newName.toHtmlEscaped());
      }
      if (newPair.newColor != oldPair.newColor) {
        auto brush = series->brush();
        brush.setColor(newPair.newColor);
        series->setBrush(brush);
      }
    }
    mSeriesMapping = newMapping;
  }
}

void PlotterBoxChart::onComboTimeUnitChanged(int /*index*/) {
  if (mIgnoreEvents)
    return;

  // Update data
  double unitFactor = ui->comboBoxTimeUnit->currentData().toDouble();
  double updateFactor = unitFactor / mCurrentTimeFactor;  // can cause precision loss
  auto chartSeries = mChartView->chart()->series();
  if (chartSeries.empty())
    return;

  for (auto& series : chartSeries) {
    QBoxPlotSeries* boxSeries = (QBoxPlotSeries*)series;
    auto boxSets = boxSeries->boxSets();
    for (int idx = 0; idx < boxSets.size(); ++idx) {
      auto* box = boxSets.at(idx);
      box->setValue(QBoxSet::LowerExtreme, box->at(QBoxSet::LowerExtreme) * updateFactor);
      box->setValue(QBoxSet::UpperExtreme, box->at(QBoxSet::UpperExtreme) * updateFactor);
      box->setValue(QBoxSet::Median, box->at(QBoxSet::Median) * updateFactor);
      box->setValue(QBoxSet::LowerQuartile, box->at(QBoxSet::LowerQuartile) * updateFactor);
      box->setValue(QBoxSet::UpperQuartile, box->at(QBoxSet::UpperQuartile) * updateFactor);
    }
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
  ui->doubleSpinBoxMin->setValue(ui->doubleSpinBoxMin->value() * updateFactor);
  ui->doubleSpinBoxMax->setValue(ui->doubleSpinBoxMax->value() * updateFactor);
  if (ui->comboBoxAxis->currentIndex() != 1 && !axes.isEmpty()) {
    QValueAxis* yAxis = (QValueAxis*)(axes.first());
    onSpinMinChanged2(yAxis->min() * updateFactor, 1);
    onSpinMaxChanged2(yAxis->max() * updateFactor, 1);
  }

  mCurrentTimeFactor = unitFactor;
}

//
// Axes
void PlotterBoxChart::onComboAxisChanged(int idx) {
  // Update UI
  bool wasIgnoring = mIgnoreEvents;
  mIgnoreEvents = true;

  ui->checkBoxAxisVisible->setChecked(mAxesParams[idx].visible);
  ui->checkBoxTitle->setChecked(mAxesParams[idx].title);
  ui->checkBoxLog->setEnabled(idx == 1);
  ui->spinBoxLogBase->setEnabled(ui->checkBoxLog->isEnabled() && ui->checkBoxLog->isChecked());
  ui->lineEditTitle->setText(mAxesParams[idx].titleText);
  ui->lineEditTitle->setCursorPosition(0);
  ui->spinBoxTitleSize->setValue(mAxesParams[idx].titleSize);
  ui->lineEditFormat->setEnabled(idx == 1);
  ui->spinBoxLabelSize->setValue(mAxesParams[idx].labelSize);
  ui->comboBoxMin->setVisible(idx == 0);
  ui->comboBoxMax->setVisible(idx == 0);
  ui->doubleSpinBoxMin->setVisible(idx == 1);
  ui->doubleSpinBoxMax->setVisible(idx == 1);
  ui->spinBoxTicks->setEnabled(idx == 1 && !ui->checkBoxLog->isChecked());
  ui->spinBoxMTicks->setEnabled(idx == 1);

  mIgnoreEvents = wasIgnoring;
}

void PlotterBoxChart::onCheckAxisVisible(int state) {
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

void PlotterBoxChart::onCheckTitleVisible(int state) {
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

void PlotterBoxChart::onCheckLog(int state) {
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

      logAxis->setBase(ui->spinBoxLogBase->value());
      logAxis->setMin(ui->doubleSpinBoxMin->value());
      logAxis->setMax(ui->doubleSpinBoxMax->value());
      logAxis->setMinorTickCount(ui->spinBoxMTicks->value());
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

      axis->setMin(ui->doubleSpinBoxMin->value());
      axis->setMax(ui->doubleSpinBoxMax->value());
      axis->setTickCount(ui->spinBoxTicks->value());
      axis->setMinorTickCount(ui->spinBoxMTicks->value());
    }
    ui->spinBoxTicks->setEnabled(state != Qt::Checked);
    ui->spinBoxLogBase->setEnabled(state == Qt::Checked);
  }
}

void PlotterBoxChart::onSpinLogBaseChanged(int i) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty() && ui->checkBoxLog->isChecked()) {
    QLogValueAxis* logAxis = (QLogValueAxis*)(axes.first());
    logAxis->setBase(i);
  }
}

void PlotterBoxChart::onEditTitleChanged(const QString& text) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();

  onEditTitleChanged2(text, iAxis);
}

void PlotterBoxChart::onEditTitleChanged2(const QString& text, int iAxis) {
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty()) {
    QAbstractAxis* axis = axes.first();
    axis->setTitleText(text);
    mAxesParams[iAxis].titleText = text;
  }
}

void PlotterBoxChart::onSpinTitleSizeChanged(int i) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();

  onSpinTitleSizeChanged2(i, iAxis);
}

void PlotterBoxChart::onSpinTitleSizeChanged2(int i, int iAxis) {
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

void PlotterBoxChart::onEditFormatChanged(const QString& text) {
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
  }
}

void PlotterBoxChart::onSpinLabelSizeChanged(int i) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();

  onSpinLabelSizeChanged2(i, iAxis);
}

void PlotterBoxChart::onSpinLabelSizeChanged2(int i, int iAxis) {
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

void PlotterBoxChart::onSpinMinChanged(double d) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();

  onSpinMinChanged2(d, iAxis);
}

void PlotterBoxChart::onSpinMinChanged2(double d, int iAxis) {
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty()) {
    QAbstractAxis* axis = axes.first();
    axis->setMin(d);
  }
}

void PlotterBoxChart::onSpinMaxChanged(double d) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();

  onSpinMaxChanged2(d, iAxis);
}

void PlotterBoxChart::onSpinMaxChanged2(double d, int iAxis) {
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty()) {
    QAbstractAxis* axis = axes.first();
    axis->setMax(d);
  }
}

void PlotterBoxChart::onComboMinChanged(int /*index*/) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty()) {
    QBarCategoryAxis* axis = (QBarCategoryAxis*)(axes.first());
    axis->setMin(ui->comboBoxMin->currentText());
  }
}

void PlotterBoxChart::onComboMaxChanged(int /*index*/) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty()) {
    QBarCategoryAxis* axis = (QBarCategoryAxis*)(axes.first());
    axis->setMax(ui->comboBoxMax->currentText());
  }
}

void PlotterBoxChart::onSpinTicksChanged(int i) {
  if (mIgnoreEvents)
    return;
  int iAxis = ui->comboBoxAxis->currentIndex();
  Qt::Orientation orient = iAxis == 0 ? Qt::Horizontal : Qt::Vertical;

  const auto& axes = mChartView->chart()->axes(orient);
  if (!axes.isEmpty()) {
    if (!ui->checkBoxLog->isChecked()) {
      QValueAxis* axis = (QValueAxis*)(axes.first());
      axis->setTickCount(i);
    }
  }
}

void PlotterBoxChart::onSpinMTicksChanged(int i) {
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
  }
}

//
// Actions
void PlotterBoxChart::onCheckAutoReload(int state) {
  if (state == Qt::Checked) {
    if (mWatcher.files().empty()) {
      mWatcher.addPath(mOrigFilename);
      for (const auto& addFilename : mAddFilenames)
        mWatcher.addPath(addFilename.filename);
    }
  } else {
    if (!mWatcher.files().empty())
      mWatcher.removePaths(mWatcher.files());
  }
}

void PlotterBoxChart::onAutoReload(const QString& path) {
  QFileInfo fi(path);
  if (fi.exists() && fi.isReadable() && fi.size() > 0)
    onReloadClicked();
  else
    qWarning() << "Unable to auto-reload file: " << path;
}

void PlotterBoxChart::onReloadClicked() {
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
      if (newSeriesIdx >= oldChartSeries.size())
        break;

      const QString& subsetName = bchSubset.name;
      if (subsetName != mSeriesMapping[newSeriesIdx].oldName) {
        errorMsg = "Series has different name";
        break;
      }
      const auto boxSeries = (QBoxPlotSeries*)oldChartSeries[newSeriesIdx];
      if (bchSubset.idxs.size() != boxSeries->count()) {
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
    newSeriesIdx = 0;
    for (const auto& bchSubset : qAsConst(newBchSubsets)) {
      // Update points
      QBoxPlotSeries* oldSeries = (QBoxPlotSeries*)oldChartSeries[newSeriesIdx];
      oldSeries->clear();

      for (int idx : bchSubset.idxs) {
        QString xName = newBchResults.getParamName(mPlotParams.xType == PlotArgumentType, idx,
                                                   mPlotParams.xIdx);
        BenchYStats yStats = getYPlotStats(newBchResults.benchmarks[idx], mPlotParams.yType);

        QScopedPointer<QBoxSet> box(new QBoxSet(xName.toHtmlEscaped()));
        box->setValue(QBoxSet::LowerExtreme, yStats.min * mCurrentTimeFactor);
        box->setValue(QBoxSet::UpperExtreme, yStats.max * mCurrentTimeFactor);
        box->setValue(QBoxSet::Median, yStats.median * mCurrentTimeFactor);
        box->setValue(QBoxSet::LowerQuartile, yStats.lowQuart * mCurrentTimeFactor);
        box->setValue(QBoxSet::UpperQuartile, yStats.uppQuart * mCurrentTimeFactor);

        oldSeries->append(box.take());
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

void PlotterBoxChart::onSnapshotClicked() {
  QString fileName =
      QFileDialog::getSaveFileName(this, tr("Save snapshot"), "", tr("Images (*.png)"));

  if (!fileName.isEmpty()) {
    QPixmap pixmap = mChartView->grab();

    bool ok = pixmap.save(fileName, "PNG");
    if (!ok)
      QMessageBox::warning(this, "Chart snapshot", "Error saving snapshot file.");
  }
}
